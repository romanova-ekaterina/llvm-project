//===- Codegen.cpp - Distributed ThinLTO functions and classes --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements functions and classes used to support the linker side
// of Distributed ThinLTO.
//
//===----------------------------------------------------------------------===//

#include "llvm/DTLTO/CodeGen.h"
#include "llvm/DTLTO/Config.h"
#include "llvm/DTLTO/Plugin.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

using namespace llvm;

namespace dtlto {

struct BitcodeNodeTy : dtltoBitcodeNodeTy {
  BitcodeNodeTy(StringSaver &Saver, StringRef ModId, StringRef ModPath, size_t ModSize,
                StringRef Triple, bool InArchive) {
    assert(!ModId.empty());
    assert(!ModPath.empty());

    ModuleId = Saver.save(ModId).data();
    ModulePath = Saver.save(ModPath).data();
    ModuleSize = ModSize;
    TargetTriple = Saver.save(Triple).data();

    SmallString<32> UID{".", utohexstr(sys::Process::getProcessId())};
    auto AssociatedPath = [&](StringRef Ext) {
      return Saver.save(Twine(ModulePath) + UID + Ext).data();
    };

    SummaryIndexPath = AssociatedPath(".thinlto.bc");
    NativeObjectPath = AssociatedPath(".native.o");

    NeedsDeletion = InArchive;
    Excluded = true; // Initially mark as excluded.
  }

  void recordImports(BumpPtrAllocator &Alloc,
                     const std::vector<std::string> &V) {
    ImportsList = Alloc.Allocate<const char *>(V.size());
    transform(V, ImportsList, [&](const auto &S) { return S.c_str(); });
  }

  void removeTempFiles(const ConfigTy &Cfg) const {
    auto Remove = [&](StringRef Path) {
      if (auto EC = sys::fs::remove(Path, true))
        Cfg.emitError("Can't remove file " + Path + ": " + EC.message());
    };
    if(Cfg.DisableTempFilesRemoval) return;
    Remove(SummaryIndexPath);
    Remove(NativeObjectPath);
    if (NeedsDeletion)
      Remove(ModulePath);
  }

  bool NeedsDeletion = false; // Node needs to be deleted after codegen
  bool Excluded = false;      // Node is excluded from code generation
};

using BitcodeNodeMapTy = MapVector<StringRef, BitcodeNodeTy>;

// Writes the contents of a buffer to a file.
static bool saveBuffer(ConfigTy &Cfg, StringRef FileBuffer,
                       StringRef FilePath) {
  SmallString<64> UniquePathModel{FilePath, ".%%%-%%%.tmp"};
  SmallString<64> TempFilePath;
  sys::fs::createUniquePath(UniquePathModel, TempFilePath, false);

  std::error_code EC;
  raw_fd_ostream OS(TempFilePath.str(), EC, sys::fs::OpenFlags::OF_None);
  if (EC) {
    Cfg.emitError("Can't create file " + TempFilePath + ": " + EC.message());
    return false;
  }
  OS.write(FileBuffer.data(), FileBuffer.size());
  OS.close();
  // Rename temporary file into a real one.
  EC = sys::fs::rename(TempFilePath, FilePath);
  if (EC) {
    sys::fs::remove(TempFilePath);
    Cfg.emitError("Can't rename file " + TempFilePath + " to " + FilePath +
                  ": " + EC.message());
    return false;
  }
  return true;
}

// Checks if the input file is a member of an archive. If it is, this function
// generates a new module ID, updates the module identifier, and saves the
// buffer with the new module ID. The input file is then added to the list of
// input files.
void ConfigTy::addInput(lto::InputFile *Input) {
  bool IsMemberOfArchive = Input->getInputFileType() ==
                           lto::InputFile::InputFileType::SOLID_ARCHIVE_MEMBER;
  if (IsMemberOfArchive) {
    StringRef ModuleId = Input->getName();
    MemoryBufferRef MemoryBufferRef = Input->getFileBuffer();
    // Generate a new module ID that is the original module ID with the process
    // ID appended.
    std::string UID = utohexstr(sys::Process::getProcessId());
    SmallString<64> NewModuleId{sys::path::filename(ModuleId), ".", UID, ".o"};
    BitcodeModule &BM = Input->getSingleBitcodeModule();
    BM.setModuleIdentifier(Saver.save(NewModuleId.str()));
    saveBuffer(*this, MemoryBufferRef.getBuffer(), NewModuleId);
  }
  InputFiles.push_back(std::unique_ptr<lto::InputFile>{Input});
}

// Initializes the bitcode modules map from the list of lto::InputFile's.
static Error initBitcodeModulesMap(ConfigTy &Cfg, BitcodeNodeMapTy &BcNodeMap) {
  BcNodeMap.reserve(Cfg.InputFiles.size());

  for (const auto &InputFile : Cfg.InputFiles) {
    StringRef ModuleId = InputFile->getName();
    SmallString<128> ModulePath = ModuleId;
    if (std::error_code EC = sys::fs::make_absolute(ModulePath))
      return createStringError("Can't make absolute path: " + EC.message());
    sys::path::remove_dots(ModulePath, true);

    bool IsMemberOfArchive =
        InputFile->getInputFileType() ==
        lto::InputFile::InputFileType::SOLID_ARCHIVE_MEMBER;
    MemoryBufferRef MbRef = InputFile->getFileBuffer();
    // Insert a new BitcodeNode into the map.
    // Be careful - the ModuleId must have a permanent storage.
    BcNodeMap.insert_or_assign(ModuleId,
                               BitcodeNodeTy(Cfg.Saver, ModuleId, ModulePath, MbRef.getBufferSize(),
                                             InputFile->getTargetTriple(),
                                             IsMemberOfArchive));
  }
  return Error::success();
}

static Error thinLink(ConfigTy &Cfg, lto::LTO &LTOObj, AddStreamFn AddStreamFunc,
                      BitcodeNodeMapTy &BcNodeMap) {
  auto ThinIndexBackend = lto::createWriteIndexesThinBackend(
      hardware_concurrency(), "", "", "", true, nullptr, nullptr);
  LTOObj.setThinBackend(ThinIndexBackend);

  size_t NumTasks = LTOObj.getMaxTasks();
  SmallVector<std::string, 0> ModuleNames(NumTasks);
  std::vector<SmallString<0>> SummaryIndexFiles(NumTasks);
  std::vector<std::vector<std::string>> ImportsFilesLists(NumTasks);

  lto::Config &LtoCfg = LTOObj.getConfig();
  LtoCfg.GetSummaryIndexStreamFunc = [&](size_t Task, StringRef ModName) {
    ModuleNames[Task] = ModName;
    return std::make_unique<raw_svector_ostream>(SummaryIndexFiles[Task]);
  };
  LtoCfg.GetImportsListRefFunc = [&](size_t Task) {
    return std::ref(ImportsFilesLists[Task]);
  };

  if (auto E = LTOObj.run(AddStreamFunc, {}))
    return E;

  // After the running the WriteIndexesThinBackend, we shall expect
  // SummaryIndexFiles - an array of buffers
  // ImportsListFiles - an array of arrays of strings
  // ModuleNames - an array of module names
  for (unsigned I = 1; I != NumTasks; ++I) {
    const StringRef ModuleId = ModuleNames[I];
    // Skip VTables. TaskNum == 0 is used for VTables
    if (ModuleId.empty())
      continue;
    BitcodeNodeTy &ModuleNode = BcNodeMap.find(ModuleId)->second;
    assert(ModuleNode.ModuleId == ModuleId);
    ModuleNode.TaskNumber = I;
    ModuleNode.Excluded = false; // Mark as included in the codegen.
    if (!SummaryIndexFiles[I].empty())
      saveBuffer(Cfg, SummaryIndexFiles[I], ModuleNode.SummaryIndexPath);
    ModuleNode.recordImports(Cfg.Alloc, ImportsFilesLists[I]);
  }
  return Error::success();
}

static Error
performCodegenWithPlugin(ConfigTy &Cfg,
                         ArrayRef<const dtltoBitcodeNodeTy *> BitcodesList,
                         ArrayRef<StringRef> Args) {
  // Load the plugin from a dynamic library.
  auto ExePath = sys::fs::getMainExecutable(Cfg.Argv0, nullptr);
  if (ExePath.empty())
    return createStringError("Executable path is empty");
  SmallString<64> PluginPath = sys::path::parent_path(ExePath);
  StringRef DbsName  = StringRef(Cfg.DbsKind).split(':').first;
#ifdef _WIN32
  const char *Prefix = "DTLTO";
#else // TODO: macOS.
  const char *Prefix = "../lib/libDTLTO";
#endif
  sys::path::append(PluginPath, Twine(Prefix) + DbsName + LLVM_PLUGIN_EXT);
  std::string ErrMsg;
  auto PluginHandle =
      sys::DynamicLibrary::getPermanentLibrary(PluginPath.c_str(), &ErrMsg);
  if (!PluginHandle.isValid())
    return createStringError("Failed to open the plugin library: %s, error: %s",
                             PluginPath.c_str(), ErrMsg.c_str());

  // Get the codegen function pointer.
  void *SymPtr = PluginHandle.getAddressOfSymbol("dtltoPerformCodegen");
  if (!SymPtr)
    return createStringError(
        "Failed to get address of the symbol: dtltoPerformCodegen");
  dtltoPerformCodegenTy PerformCodegen =
      reinterpret_cast<dtltoPerformCodegenTy>(SymPtr);

  dtltoConfigTy PluginCfg{
      Cfg.DbsKind.c_str(), reinterpret_cast<const dtltoDiagContextTy *>(&Cfg),
      [](const dtltoDiagContextTy *Cfg, const char *E) -> void {
        reinterpret_cast<const ConfigTy *>(Cfg)->emitError(E);
      },
      [](const dtltoDiagContextTy *Cfg, const char *W) -> void {
        reinterpret_cast<const ConfigTy *>(Cfg)->emitWarn(W);
      }};

  std::vector<const char *> Argv(Args.size());
  transform(Args, Argv.begin(), [&](const auto &S) { return S.data(); });

  // Call plugin's dtltoPerformCodegen function.
  if (int Code = PerformCodegen(&PluginCfg, BitcodesList.size(),
                                BitcodesList.data(), Argv.size(), Argv.data()))
    return createStringError(
        "Failed to perform codegen with DTLTO plugin: %s, error code: %d",
        Cfg.DbsKind.c_str(), Code);
  return Error::success();
}

// Maps code generation options to clang options.
static Error mapCGOptionsToClangOptions(StringSaver &Saver,
                                        const lto::Config &Config,
                                        SmallVectorImpl<StringRef> &Ops) {
  Ops.push_back(Saver.save("-O" + Twine(Config.OptLevel)));
  if (Config.Options.EmitAddrsig)
    Ops.push_back("-faddrsig");
  if (Config.Options.FunctionSections)
    Ops.push_back("-ffunction-sections");
  if (Config.Options.DataSections)
    Ops.push_back("-fdata-sections");
  if (Config.Options.UniqueBasicBlockSectionNames)
    Ops.push_back("-funique-basic-block-section-names");
  if (Config.Options.FloatABIType == FloatABI::Hard)
    Ops.push_back("-ffp-model=hard");
  else if (Config.Options.FloatABIType == FloatABI::Soft)
    Ops.push_back("-ffp-model=soft");
  if (Config.RelocModel == Reloc::PIC_) {
    // FIXME: this assumes host == target
#ifndef _WIN32 // Windows target does not support -fpic
    Ops.push_back("-fpic");
#endif
  } else if (Config.RelocModel == Reloc::ROPI)
    Ops.push_back("-fropi");
  else if (Config.RelocModel == Reloc::RWPI)
    Ops.push_back("-frwpi");
  if (Config.CodeModel == CodeModel::Kernel)
    Ops.push_back("-mcmodel=kernel");
  else if (Config.CodeModel == CodeModel::Large)
    Ops.push_back("-mcmodel=large");
  else if (Config.CodeModel == CodeModel::Medium)
    Ops.push_back("-mcmodel=medium");
  else if (Config.CodeModel == CodeModel::Small)
    Ops.push_back("-mcmodel=small");
  // Since we do not know which of those options will be used by the clang,
  // disable the corresponding warning message.
  Ops.push_back("-Wno-unused-command-line-argument");
  // Turn on/off warnings about profile cfg mismatch (default on)
  // --lto-pgo-warn-mismatch.
  if (!Config.PGOWarnMismatch) {
    Ops.push_back("-mllvm");
    Ops.push_back("-no-pgo-warn-mismatch");
  }
  // Perform context sensitive PGO instrumentation --lto-cs-profile-generate.
  // Generate instrumented code to collect execution counts into default.profraw
  // file. File name can be overridden by LLVM_PROFILE_FILE env variable.
  if (Config.RunCSIRInstr)
    Ops.push_back("-fcs-profile-generate");

  return Error::success();
}

// Performs DTLTO code generation.
// This function initializes the configuration, performs the ThinLink phase,
// maps code generation options, and executes the code generation process.
// Temporary files are removed if the configuration allows it.
Error codeGenImpl(ConfigTy &Cfg, lto::LTO &LTOObj,
          AddStreamFn AddStreamFunc, AddBufferFn AddBufferFunc) {
  LTOObj.setLTOMode(lto::LTO::LTOKind::LTOK_UnifiedThin);
  BitcodeNodeMapTy BcNodeMap;
  if (auto E = initBitcodeModulesMap(Cfg, BcNodeMap))
    return E;

  struct RemoveTempFiles {
    const ConfigTy &Cfg;
    const BitcodeNodeMapTy &BcNodeMap;
    RemoveTempFiles(const ConfigTy &C, const BitcodeNodeMapTy &BNM)
        : Cfg(C), BcNodeMap(BNM) {}
    ~RemoveTempFiles() {
      // TODO: If the number of files for removal is large, we could consider
      // using a thread pool.
      for (const auto &[ModId, BcNode] : BcNodeMap)
        BcNode.removeTempFiles(Cfg);
    }
  } RTF(Cfg, BcNodeMap);

  if (auto E = thinLink(Cfg, LTOObj, AddStreamFunc, BcNodeMap))
    return E;
  SmallVector<StringRef, 8> Args;
  if (auto E = mapCGOptionsToClangOptions(Cfg.Saver, LTOObj.getConfig(), Args))
    return E;

  LTOObj.getConfig().AlwaysEmitRegularLTOObj = true;

  struct GtSizeOp {
    bool operator()(const dtltoBitcodeNodeTy *L, const dtltoBitcodeNodeTy *R) const {
      return L->ModuleSize > R->ModuleSize;
    }
  };

  std::vector<const dtltoBitcodeNodeTy *> BitcodesList;
  for (const auto &BitcodeNode : BcNodeMap) {
    if (BitcodeNode.second.Excluded)
      continue;
    BitcodesList.push_back(&BitcodeNode.second);
  }
  std::sort(BitcodesList.begin(), BitcodesList.end(), GtSizeOp());

  Error E = performCodegenWithPlugin(Cfg, BitcodesList, Args);
  if (E)
    return joinErrors(
        std::move(E),
        createStringError("Codegen failed on distributed build system: %s.",
                          Cfg.DbsKind.data()));

  for (auto &[ModuleId, BitcodeNode] : BcNodeMap) {
    if (BitcodeNode.Excluded)
      continue;
    if (!sys::fs::exists(BitcodeNode.NativeObjectPath)) {
      E = joinErrors(std::move(E), errorCodeToError(std::make_error_code(
                                       std::errc::no_such_file_or_directory)));
      continue;
    }
    ErrorOr<std::unique_ptr<MemoryBuffer>> ObjectBuffer =
        MemoryBuffer::getFile(BitcodeNode.NativeObjectPath);
    if (std::error_code EC = ObjectBuffer.getError()) {
      E = joinErrors(std::move(E),
                     createStringError(EC, "Can't read file %s",
                                       BitcodeNode.NativeObjectPath));
      continue;
    }

    BitcodeNode.Excluded = true;
    AddBufferFunc(BitcodeNode.TaskNumber, BitcodeNode.ModuleId,
                  std::move(ObjectBuffer.get()));
  }
  return E;
}

// Performs DTLTO code generation for ELF format.
Error codeGenELF(ConfigTy &Cfg, lto::LTO &LtoObj,
                 llvm::SmallVector<std::pair<std::string, llvm::SmallString<0>>,
                                   0> &OutputStreams,
                 std::vector<std::unique_ptr<MemoryBuffer>> &OutputBuffers,
                 SmallVector<std::string, 0> &ModuleNames) {

  return codeGenImpl(
      Cfg, LtoObj,
      [&](size_t Task, const llvm::Twine &ModuleName) {
        OutputStreams[Task].first = ModuleName.str();
        return std::make_unique<llvm::CachedFileStream>(
            std::make_unique<llvm::raw_svector_ostream>(
                OutputStreams[Task].second));
      },
      [&](size_t Task, const Twine &ModuleName,
          std::unique_ptr<MemoryBuffer> MBuf) {
        OutputBuffers[Task] = std::move(MBuf);
        ModuleNames[Task] = ModuleName.str();
      });
}

// Performs DTLTO code generation for COFF format.
Error codeGenCOFF(
    ConfigTy &Cfg, lto::LTO &LtoObj,
    std::vector<std::pair<std::string, llvm::SmallString<0>>> &OutputStreams,
    std::vector<std::unique_ptr<MemoryBuffer>> &OutputBuffers,
    std::vector<std::string> &ModuleNames) {

  return codeGenImpl(
      Cfg, LtoObj,
      [&](size_t Task, const llvm::Twine &ModuleName) {
        OutputStreams[Task].first = ModuleName.str();
        return std::make_unique<llvm::CachedFileStream>(
            std::make_unique<llvm::raw_svector_ostream>(
                OutputStreams[Task].second));
      },
      [&](size_t Task, const Twine &ModuleName,
          std::unique_ptr<MemoryBuffer> MBuf) {
        OutputBuffers[Task] = std::move(MBuf);
        ModuleNames[Task] = ModuleName.str();
      });
}

} // namespace dtlto
