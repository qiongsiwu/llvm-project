
// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0.0 -O0 -fbounds-safety -S %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0.0 -O0 -fbounds-safety -x objective-c -fexperimental-bounds-safety-objc -S %s -o - | FileCheck %s

#include <ptrcheck.h>

union Foo {
    char *__bidi_indexable inner;
    int a;
    unsigned b[100];
};
char globalCh;

union Foo global = {
    .inner = &globalCh
};

// CHECK: _global:
// CHECK: 	.quad	_globalCh
// CHECK: 	.quad	_globalCh+1
// CHECK: 	.quad	_globalCh