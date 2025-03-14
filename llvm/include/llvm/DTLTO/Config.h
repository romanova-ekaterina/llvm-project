//===- Config.h - Distributed ThinLTO functions and classes------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DTLTO_CONFIG_H
#define LLVM_DTLTO_CONFIG_H

#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/StringSaver.h"

namespace dtlto {

class ConfigTy {
  struct DiagnosticInfo : public llvm::DiagnosticInfo {
    DiagnosticInfo(const llvm::Twine &Msg, llvm::DiagnosticSeverity Severity)
        : llvm::DiagnosticInfo(llvm::DiagnosticKind::DK_Linker, Severity),
          DiagMsg(Msg) {}

    void print(llvm::DiagnosticPrinter &DP) const override { DP << DiagMsg; }

    const llvm::Twine &DiagMsg;
  };

public:
  llvm::DiagnosticHandlerFunction DiagHandler =
      nullptr;         ///< Diagnostic handler function
  const char *Argv0;   // Executable argv[0].
  std::string DbsKind; // Name of the distributed system as specified in the
                       // linker command line
  bool DisableTempFilesRemoval = false;                     
  std::vector<std::unique_ptr<llvm::lto::InputFile>>
      InputFiles; // Array of input bitcode files for LTO

  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver Saver{Alloc};

  void addInput(llvm::lto::InputFile *Input);

  void emitError(const llvm::Twine &Msg) const {
    DiagHandler(DiagnosticInfo(Msg, llvm::DiagnosticSeverity::DS_Error));
  }
  void emitWarn(const llvm::Twine &Msg) const {
    DiagHandler(DiagnosticInfo(Msg, llvm::DiagnosticSeverity::DS_Warning));
  }
};

} // namespace dtlto

#endif // LLVM_DTLTO_CONFIG_H
