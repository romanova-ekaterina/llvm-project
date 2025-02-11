//===- Local.cpp - Distributed ThinLTO functions and classes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DTLTO/Plugin.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ThreadPool.h"

using namespace llvm;

// For test only. Creates "mock" output files.
int dtltoPerformMockCodegen(const struct dtltoConfigTy *Cfg, size_t NodesNum,
                            const dtltoBitcodeNodeTy *const *BitcodeNodes) {
  auto createFile = [&](StringRef Path, StringRef Content) {
    std::error_code EC;
    auto OS = std::make_unique<llvm::raw_fd_ostream>(
        Path, EC, llvm::sys::fs::CD_CreateNew);
    if (EC) {
      Cfg->EmitError(Cfg->DiagContext, "Error creating file.");
    }
    OS->write(Content.data(), Content.size());
  };
  for (size_t N = 0; N < NodesNum; ++N) {
    createFile(BitcodeNodes[N]->NativeObjectPath, BitcodeNodes[N]->ModuleId);
  }
  return 0;
}

extern "C" DTLTO_PLUGIN_EXPORT int
dtltoPerformCodegen(const dtltoConfigTy *Cfg, size_t NodesNum,
                    const dtltoBitcodeNodeTy *const *BitcodeNodes, size_t Argc,
                    const char **Argv) {
  ErrorOr<std::string> ClangPath = sys::findProgramByName("clang");
  if (!ClangPath) {
    SmallString<64> Err{"Failed to find clang: ",
                        ClangPath.getError().message()};
    Cfg->EmitError(Cfg->DiagContext, Err.c_str());
    return ClangPath.getError().value();
  }

  SmallVector<StringRef, 0> CommonArgs(Argc + 1);
  CommonArgs[0] = *ClangPath;
  for (size_t I = 0; I != Argc; ++I)
    CommonArgs[I + 1] = Argv[I];

  if (StringRef(Cfg->DbsName).contains("::test"))
    return dtltoPerformMockCodegen(Cfg, NodesNum, BitcodeNodes);

  struct ArgsListTy {
    SmallVector<StringRef> Refs;

    void add(ArrayRef<StringRef> Args) {
      Refs.insert(Refs.end(), Args.begin(), Args.end());
    }
  };

  DefaultThreadPool Pool(hardware_concurrency());
  std::mutex ErMutex;
  Error ER{Error::success()};
  std::atomic<size_t> NodeIdx = 0;
  size_t TNum = std::min(size_t(Pool.getMaxConcurrency()), NodesNum);
  for (size_t T = 0; T < TNum; ++T) {
    Pool.async([&]() {
      for (;;) {
        size_t I = NodeIdx.fetch_add(1);
        if (I >= NodesNum)
          return;

        // Build child process command.
        const dtltoBitcodeNodeTy *Node = BitcodeNodes[I];
        ArgsListTy Args;
        Args.add(CommonArgs);
        SmallString<64> IdxArg{"-fthinlto-index=", Node->SummaryIndexPath};
        Args.add({"-Wno-invalid-or-nonexistent-directory", "-c", "-x", "ir",
                  IdxArg, "-target", Node->TargetTriple, "-o",
                  Node->NativeObjectPath, Node->ModuleId});

        // Execute child process.
        std::string Err;
        if (sys::ExecuteAndWait(Args.Refs[0], Args.Refs, {}, {}, 0, 0, &Err,
                                nullptr) != 0) {
          std::lock_guard<std::mutex> Lock(ErMutex);
          ER = joinErrors(std::move(ER),
                          createStringError("%s: error: %s", ClangPath->c_str(),
                                            Err.c_str()));
        }
      }
    });
  }
  Pool.wait();

  if (ER) {
    handleAllErrors(std::move(ER), [&](const ErrorInfoBase &E) -> Error {
      Cfg->EmitError(Cfg->DiagContext, E.message().data());
      return Error::success();
    });
    return 1;
  }

  return 0;
}
