//===- Plugin.h - Distributed ThinLTO functions and classes -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DTLTO_PLUGIN_H
#define LLVM_DTLTO_PLUGIN_H

#include <stddef.h>

#ifdef _WIN32
#define DTLTO_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DTLTO_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

struct dtltoDiagContextTy;
typedef void (*dtltoEmitDiagTy)(const dtltoDiagContextTy *, const char *);

typedef struct dtltoConfigTy {
  const char *DbsName;
  const dtltoDiagContextTy *DiagContext;
  dtltoEmitDiagTy EmitError;
  dtltoEmitDiagTy EmitWarn;
} dtltoConfigTy;

typedef struct dtltoBitcodeNodeTy {
  const char *ModuleId;
  const char *ModulePath;
  size_t ModuleSize;
  const char *SummaryIndexPath;
  const char *NativeObjectPath;
  size_t ImportsListSize;
  const char **ImportsList;
  const char *TargetTriple;
  size_t TaskNumber;
} dtltoBitcodeNodeTy;

// Prepares for code generation on the distributed build system.
typedef int (*dtltoPerformCodegenTy)(
    const dtltoConfigTy *Cfg, size_t NodesNum,
    const dtltoBitcodeNodeTy *const *BitcodeNodes, size_t Argc,
    const char **Argv);

} // extern "C"

#endif // LLVM_DTLTO_PLUGIN_H
