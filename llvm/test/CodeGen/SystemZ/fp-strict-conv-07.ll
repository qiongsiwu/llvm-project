; Test strict conversions of signed i64s to floating-point values.
;
; RUN: llc < %s -mtriple=s390x-linux-gnu | FileCheck %s

declare half @llvm.experimental.constrained.sitofp.f16.i64(i64, metadata, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i64(i64, metadata, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i64(i64, metadata, metadata)
declare fp128 @llvm.experimental.constrained.sitofp.f128.i64(i64, metadata, metadata)

; Test i64->f16.
define half @f0(i64 %i) #0 {
; CHECK-LABEL: f0:
; CHECK: cegbr %f0, %r2
; CHECK-NEXT: brasl %r14, __truncsfhf2@PLT
; CHECK: br %r14
  %conv = call half @llvm.experimental.constrained.sitofp.f16.i64(i64 %i,
                                               metadata !"round.dynamic",
                                               metadata !"fpexcept.strict") #0
  ret half %conv
}

; Test i64->f32.
define float @f1(i64 %i) #0 {
; CHECK-LABEL: f1:
; CHECK: cegbr %f0, %r2
; CHECK: br %r14
  %conv = call float @llvm.experimental.constrained.sitofp.f32.i64(i64 %i,
                                               metadata !"round.dynamic",
                                               metadata !"fpexcept.strict") #0
  ret float %conv
}

; Test i64->f64.
define double @f2(i64 %i) #0 {
; CHECK-LABEL: f2:
; CHECK: cdgbr %f0, %r2
; CHECK: br %r14
  %conv = call double @llvm.experimental.constrained.sitofp.f64.i64(i64 %i,
                                               metadata !"round.dynamic",
                                               metadata !"fpexcept.strict") #0
  ret double %conv
}

; Test i64->f128.
define void @f3(i64 %i, ptr %dst) #0 {
; CHECK-LABEL: f3:
; CHECK: cxgbr %f0, %r2
; CHECK: std %f0, 0(%r3)
; CHECK: std %f2, 8(%r3)
; CHECK: br %r14
  %conv = call fp128 @llvm.experimental.constrained.sitofp.f128.i64(i64 %i,
                                               metadata !"round.dynamic",
                                               metadata !"fpexcept.strict") #0
  store fp128 %conv, ptr %dst
  ret void
}

attributes #0 = { strictfp }
