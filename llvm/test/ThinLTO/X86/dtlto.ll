;; Test DTLTO build thin link output from llvm-lto2

; RUN: rm -rf %t && split-file %s %t && cd %t

;; Generate bitcode files with summary
; RUN: opt -thinlto-bc t1.ll -o t1.bc
; RUN: opt -thinlto-bc t2.ll -o t2.bc

;; Perform DTLTO. Does not do any compilation,
;; instead it creates 'mock' the native object files.
; RUN: llvm-lto2 run t1.bc t2.bc -o t.o \
; RUN:     -thinlto-distribute=local::test \
; RUN:     -r=t1.bc,g, \
; RUN:     -r=t1.bc,analias, \
; RUN:     -r=t1.bc,f,px \
; RUN:     -r=t2.bc,g,px \
; RUN:     -r=t2.bc,analias,px \
; RUN:     -r=t2.bc,aliasee,px

;; Check that the requried output files have been created.
; RUN: ls * | FileCheck %s --check-prefix=OUTPUT

; OUTPUT-DAG: t1.bc.{{[0-9A-F]+}}.native.o{{$}}
; OUTPUT-DAG: t1.bc.{{[0-9A-F]+}}.thinlto.bc{{$}}

; OUTPUT-DAG: t2.bc.{{[0-9A-F]+}}.native.o{{$}}
; OUTPUT-DAG: t2.bc.{{[0-9A-F]+}}.thinlto.bc{{$}}


;--- t1.ll

target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"

declare i32 @g(...)
declare void @analias(...)

define void @f() {
entry:
  call i32 (...) @g()
  call void (...) @analias()
  ret void
}

!llvm.dbg.cu = !{}

!1 = !{i32 2, !"Debug Info Version", i32 3}
!llvm.module.flags = !{!1}


;--- t2.ll

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@G = internal global i32 7
define i32 @g() {
entry:
  %0 = load i32, ptr @G
  ret i32 %0
}

@analias = alias void (...), ptr @aliasee
define void @aliasee() {
entry:
      ret void
}
