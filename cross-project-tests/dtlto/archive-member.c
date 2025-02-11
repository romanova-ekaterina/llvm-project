/// Test that DTLTO supports archives. 

// RUN: rm -rf %t && mkdir %t && cd %t

// RUN: %clang -target x86_64-linux-gnu %s -flto=thin -c -o archive-member.o
// RUN: llvm-ar cr solid-archive.a archive-member.o

// RUN: ld.lld \
// RUN:   solid-archive.a \
// RUN:   -build-id=none \
// RUN:   -u main \
// RUN:   --thinlto-distribute=local 

int _start() {return 0;}
