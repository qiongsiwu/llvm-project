// RUN: rm -rf %t
// RUN: mkdir %t
//
// RUN: %clang -fsyntax-only %s -index-store-path %t/idx -o %t/syntax-only.c.myoutfile
// RUN: c-index-test core -print-unit %t/idx | FileCheck %s -check-prefix=CHECK-UNIT
// RUN: c-index-test core -print-record %t/idx | FileCheck %s -check-prefix=CHECK-RECORD

// CHECK-UNIT: out-file: {{.*}}/syntax-only.c.myoutfile
// CHECK-RECORD: function/C | foo | c:@F@foo

void foo();
