// RUN: %clang_cc1 -O0 -debug-info-kind=standalone -dwarf-version=5 \
// RUN:   -fbounds-safety -fbounds-safety-debug-trap-reasons=detailed \
// RUN:   -emit-llvm %s -o - -fbounds-safety-soft-traps=call-with-str | \
// RUN:   FileCheck --check-prefixes=CHECK,DETAILED %s

// RUN: %clang_cc1 -O0 -debug-info-kind=standalone -dwarf-version=5 \
// RUN:   -fbounds-safety -fbounds-safety-debug-trap-reasons=basic \
// RUN:   -emit-llvm %s -o - -fbounds-safety-soft-traps=call-with-str | \
// RUN:   FileCheck --check-prefixes=CHECK,BASIC %s

int operation(int index) {
  int array[] = {0, 1, 2};
  return array[index];
}

// DETAILED: [[TRAP_REASON_STR:@.+]] = private unnamed_addr constant [45 x i8] c"indexing below lower bound in 'array[index]'\00"
// BASIC: [[TRAP_REASON_STR:@.+]] = private unnamed_addr constant [27 x i8] c"Dereferencing below bounds\00"


// CHECK-LABEL: @operation
// We don't try to match the registers used in the comparison because trying
// to match the IR is very fragile.
// CHECK: [[BRANCH_REG:%[0-9]+]] = icmp uge ptr %{{.+}}, %{{.+}}, !dbg [[LOC:![0-9]+]]
// CHECK: br i1 [[BRANCH_REG]], label {{.+}}, label %[[TRAP_LABEL:[a-z0-9]+]], !dbg [[LOC]]
// CHECK: [[TRAP_LABEL]]:
// CHECK-NEXT: call void @__bounds_safety_soft_trap_s(ptr [[TRAP_REASON_STR]]) {{.*}}!dbg [[TRAP_LOC:![0-9]+]]

// CHECK-DAG: [[TRAP_LOC]] = !DILocation(line: 0, scope: [[TRAP_SCOPE:![0-9]+]], inlinedAt: [[LOC]])

// BASIC-DAG: [[TRAP_SCOPE]] = distinct !DISubprogram(name: "__clang_trap_msg$Bounds check failed$Dereferencing below bounds", scope: [[FILE_SCOPE:![0-9]+]], file: [[FILE_SCOPE]], type: {{.+}}, flags: DIFlagArtificial, spFlags: DISPFlagDefinition, unit: {{![0-9]+}}
// DETAILED-DAG: [[TRAP_SCOPE]] = distinct !DISubprogram(name: "__clang_trap_msg$Bounds check failed$indexing below lower bound in 'array[index]'", scope: [[FILE_SCOPE:![0-9]+]], file: [[FILE_SCOPE]], type: {{.+}}, flags: DIFlagArtificial, spFlags: DISPFlagDefinition, unit: {{![0-9]+}}

// CHECK-DAG: [[LOC]] = !DILocation(line: 13, column: 10, scope: {{![0-9]+}})

