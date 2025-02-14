; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=riscv32 -mattr=+m,+d,+zvfh,+v -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64 -mattr=+m,+d,+zvfh,+v -verify-machineinstrs < %s | FileCheck %s

define <vscale x 8 x i1> @not_icmp_sle_nxv8i16(<vscale x 8 x i16> %a, <vscale x 8 x i16> %b) {
; CHECK-LABEL: not_icmp_sle_nxv8i16:
; CHECK:       # %bb.0:
; CHECK-NEXT:    vsetvli a0, zero, e16, m2, ta, ma
; CHECK-NEXT:    vmslt.vv v0, v10, v8
; CHECK-NEXT:    ret
  %icmp = icmp sle <vscale x 8 x i16> %a, %b
  %not = xor <vscale x 8 x i1> splat (i1 true), %icmp
  ret <vscale x 8 x i1> %not
}

define <vscale x 4 x i1> @not_icmp_sgt_nxv4i32(<vscale x 4 x i32> %a, <vscale x 4 x i32> %b) {
; CHECK-LABEL: not_icmp_sgt_nxv4i32:
; CHECK:       # %bb.0:
; CHECK-NEXT:    vsetvli a0, zero, e32, m2, ta, ma
; CHECK-NEXT:    vmsle.vv v0, v8, v10
; CHECK-NEXT:    ret
  %icmp = icmp sgt <vscale x 4 x i32> %a, %b
  %not = xor <vscale x 4 x i1> %icmp, splat (i1 true)
  ret <vscale x 4 x i1> %not
}

define <vscale x 2 x i1> @not_fcmp_une_nxv2f64(<vscale x 2 x double> %a, <vscale x 2 x double> %b) {
; CHECK-LABEL: not_fcmp_une_nxv2f64:
; CHECK:       # %bb.0:
; CHECK-NEXT:    vsetvli a0, zero, e64, m2, ta, ma
; CHECK-NEXT:    vmfeq.vv v0, v8, v10
; CHECK-NEXT:    ret
  %icmp = fcmp une <vscale x 2 x double> %a, %b
  %not = xor <vscale x 2 x i1> %icmp, splat (i1 true)
  ret <vscale x 2 x i1> %not
}

define <vscale x 4 x i1> @not_fcmp_uge_nxv4f32(<vscale x 4 x float> %a, <vscale x 4 x float> %b) {
; CHECK-LABEL: not_fcmp_uge_nxv4f32:
; CHECK:       # %bb.0:
; CHECK-NEXT:    vsetvli a0, zero, e32, m2, ta, ma
; CHECK-NEXT:    vmflt.vv v0, v8, v10
; CHECK-NEXT:    ret
  %icmp = fcmp uge <vscale x 4 x float> %a, %b
  %not = xor <vscale x 4 x i1> %icmp, splat (i1 true)
  ret <vscale x 4 x i1> %not
}
