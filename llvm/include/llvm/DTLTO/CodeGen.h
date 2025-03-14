//===- CodeGen.h - Distributed ThinLTO functions and classes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DTLTO_CODEGEN_H
#define LLVM_DTLTO_CODEGEN_H

#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBuffer.h"

namespace dtlto {

class ConfigTy;

llvm::Error codeGenImpl(ConfigTy &Cfg, llvm::lto::LTO &LTOObj,
                        llvm::AddStreamFn AddStreamFunc,
                        llvm::AddBufferFn AddBufferFunc);

llvm::Error
codeGenELF(ConfigTy &Cfg, llvm::lto::LTO &LtoObj,
           llvm::SmallVector<std::pair<std::string, llvm::SmallString<0>>, 0>
               &OutputStreams,
           std::vector<std::unique_ptr<llvm::MemoryBuffer>> &OutputBuffers,
           llvm::SmallVector<std::string, 0> &ModuleNames);

llvm::Error codeGenCOFF(
    ConfigTy &Cfg, llvm::lto::LTO &LtoObj,
    std::vector<std::pair<std::string, llvm::SmallString<0>>> &OutputStreams,
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> &OutputBuffers,
    std::vector<std::string> &ModuleNames);

} // namespace dtlto

#endif // LLVM_DTLTO_CODEGEN_H
