; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=amdgcn--amdpal -mcpu=bonaire < %s | FileCheck -enable-var-scope --check-prefix=CI %s
; RUN: llc -mtriple=amdgcn--amdpal -mcpu=gfx900 < %s | FileCheck -enable-var-scope --check-prefix=GFX9 %s
; RUN: llc -mtriple=amdgcn--amdpal -mcpu=gfx1010 < %s | FileCheck -enable-var-scope --check-prefix=GFX10 %s
; RUN: llc -mtriple=amdgcn--amdpal -mcpu=gfx1100 -mattr=+real-true16 < %s | FileCheck -enable-var-scope --check-prefixes=GFX11,GFX11-TRUE16 %s
; RUN: llc -mtriple=amdgcn--amdpal -mcpu=gfx1100 -mattr=-real-true16 < %s | FileCheck -enable-var-scope --check-prefixes=GFX11,GFX11-FAKE16 %s

declare i32 @llvm.amdgcn.workitem.id.x() #0

@lds.obj = addrspace(3) global [256 x i32] poison, align 4

define amdgpu_kernel void @write_ds_sub0_offset0_global() #0 {
; CI-LABEL: write_ds_sub0_offset0_global:
; CI:       ; %bb.0: ; %entry
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0, v0
; CI-NEXT:    v_mov_b32_e32 v1, 0x7b
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b32 v0, v1 offset:12
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: write_ds_sub0_offset0_global:
; GFX9:       ; %bb.0: ; %entry
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v0, 0, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 0x7b
; GFX9-NEXT:    ds_write_b32 v0, v1 offset:12
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: write_ds_sub0_offset0_global:
; GFX10:       ; %bb.0: ; %entry
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 0x7b
; GFX10-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX10-NEXT:    ds_write_b32 v0, v1 offset:12
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: write_ds_sub0_offset0_global:
; GFX11:       ; %bb.0: ; %entry
; GFX11-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_dual_mov_b32 v1, 0x7b :: v_dual_lshlrev_b32 v0, 2, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX11-NEXT:    ds_store_b32 v0, v1 offset:12
; GFX11-NEXT:    s_endpgm
entry:
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #1
  %sub1 = sub i32 0, %x.i
  %tmp0 = getelementptr [256 x i32], ptr addrspace(3) @lds.obj, i32 0, i32 %sub1
  %arrayidx = getelementptr inbounds i32, ptr addrspace(3) %tmp0, i32 3
  store i32 123, ptr addrspace(3) %arrayidx
  ret void
}

define amdgpu_kernel void @write_ds_sub0_offset0_global_clamp_bit(float %dummy.val) #0 {
; CI-LABEL: write_ds_sub0_offset0_global_clamp_bit:
; CI:       ; %bb.0: ; %entry
; CI-NEXT:    s_load_dword s0, s[4:5], 0x0
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0, v0
; CI-NEXT:    s_mov_b64 vcc, 0
; CI-NEXT:    s_waitcnt lgkmcnt(0)
; CI-NEXT:    v_mov_b32_e32 v1, s0
; CI-NEXT:    v_mov_b32_e32 v2, 0x7b
; CI-NEXT:    v_div_fmas_f32 v1, v1, v1, v1
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    s_mov_b64 s[0:1], 0
; CI-NEXT:    s_mov_b32 s3, 0xf000
; CI-NEXT:    s_mov_b32 s2, -1
; CI-NEXT:    ds_write_b32 v0, v2 offset:12
; CI-NEXT:    buffer_store_dword v1, off, s[0:3], 0
; CI-NEXT:    s_waitcnt vmcnt(0)
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: write_ds_sub0_offset0_global_clamp_bit:
; GFX9:       ; %bb.0: ; %entry
; GFX9-NEXT:    s_load_dword s0, s[4:5], 0x0
; GFX9-NEXT:    s_mov_b64 vcc, 0
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v3, 0, v0
; GFX9-NEXT:    v_mov_b32_e32 v4, 0x7b
; GFX9-NEXT:    s_waitcnt lgkmcnt(0)
; GFX9-NEXT:    v_mov_b32_e32 v1, s0
; GFX9-NEXT:    v_div_fmas_f32 v2, v1, v1, v1
; GFX9-NEXT:    v_mov_b32_e32 v0, 0
; GFX9-NEXT:    v_mov_b32_e32 v1, 0
; GFX9-NEXT:    ds_write_b32 v3, v4 offset:12
; GFX9-NEXT:    global_store_dword v[0:1], v2, off
; GFX9-NEXT:    s_waitcnt vmcnt(0)
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: write_ds_sub0_offset0_global_clamp_bit:
; GFX10:       ; %bb.0: ; %entry
; GFX10-NEXT:    s_load_dword s0, s[4:5], 0x0
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    s_mov_b32 vcc_lo, 0
; GFX10-NEXT:    v_mov_b32_e32 v3, 0x7b
; GFX10-NEXT:    v_sub_nc_u32_e32 v2, 0, v0
; GFX10-NEXT:    v_mov_b32_e32 v0, 0
; GFX10-NEXT:    v_mov_b32_e32 v1, 0
; GFX10-NEXT:    ds_write_b32 v2, v3 offset:12
; GFX10-NEXT:    s_waitcnt lgkmcnt(0)
; GFX10-NEXT:    v_div_fmas_f32 v4, s0, s0, s0
; GFX10-NEXT:    global_store_dword v[0:1], v4, off
; GFX10-NEXT:    s_waitcnt_vscnt null, 0x0
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: write_ds_sub0_offset0_global_clamp_bit:
; GFX11:       ; %bb.0: ; %entry
; GFX11-NEXT:    s_load_b32 s0, s[4:5], 0x0
; GFX11-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-NEXT:    s_mov_b32 vcc_lo, 0
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_dual_mov_b32 v3, 0x7b :: v_dual_lshlrev_b32 v0, 2, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v2, 0, v0
; GFX11-NEXT:    v_mov_b32_e32 v0, 0
; GFX11-NEXT:    v_mov_b32_e32 v1, 0
; GFX11-NEXT:    ds_store_b32 v2, v3 offset:12
; GFX11-NEXT:    s_waitcnt lgkmcnt(0)
; GFX11-NEXT:    v_div_fmas_f32 v4, s0, s0, s0
; GFX11-NEXT:    global_store_b32 v[0:1], v4, off dlc
; GFX11-NEXT:    s_waitcnt_vscnt null, 0x0
; GFX11-NEXT:    s_endpgm
entry:
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #1
  %sub1 = sub i32 0, %x.i
  %tmp0 = getelementptr [256 x i32], ptr addrspace(3) @lds.obj, i32 0, i32 %sub1
  %arrayidx = getelementptr inbounds i32, ptr addrspace(3) %tmp0, i32 3
  store i32 123, ptr addrspace(3) %arrayidx
  %fmas = call float @llvm.amdgcn.div.fmas.f32(float %dummy.val, float %dummy.val, float %dummy.val, i1 false)
  store volatile float %fmas, ptr addrspace(1) null
  ret void
}

define amdgpu_kernel void @write_ds_sub_max_offset_global_clamp_bit(float %dummy.val) #0 {
; CI-LABEL: write_ds_sub_max_offset_global_clamp_bit:
; CI:       ; %bb.0:
; CI-NEXT:    s_load_dword s0, s[4:5], 0x0
; CI-NEXT:    s_mov_b64 vcc, 0
; CI-NEXT:    v_mov_b32_e32 v1, 0x7b
; CI-NEXT:    v_mov_b32_e32 v2, 0
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    s_waitcnt lgkmcnt(0)
; CI-NEXT:    v_mov_b32_e32 v0, s0
; CI-NEXT:    v_div_fmas_f32 v0, v0, v0, v0
; CI-NEXT:    s_mov_b64 s[0:1], 0
; CI-NEXT:    s_mov_b32 s3, 0xf000
; CI-NEXT:    s_mov_b32 s2, -1
; CI-NEXT:    ds_write_b32 v2, v1
; CI-NEXT:    buffer_store_dword v0, off, s[0:3], 0
; CI-NEXT:    s_waitcnt vmcnt(0)
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: write_ds_sub_max_offset_global_clamp_bit:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    s_load_dword s0, s[4:5], 0x0
; GFX9-NEXT:    s_mov_b64 vcc, 0
; GFX9-NEXT:    v_mov_b32_e32 v3, 0x7b
; GFX9-NEXT:    v_mov_b32_e32 v4, 0
; GFX9-NEXT:    ds_write_b32 v4, v3
; GFX9-NEXT:    s_waitcnt lgkmcnt(0)
; GFX9-NEXT:    v_mov_b32_e32 v0, s0
; GFX9-NEXT:    v_div_fmas_f32 v2, v0, v0, v0
; GFX9-NEXT:    v_mov_b32_e32 v0, 0
; GFX9-NEXT:    v_mov_b32_e32 v1, 0
; GFX9-NEXT:    global_store_dword v[0:1], v2, off
; GFX9-NEXT:    s_waitcnt vmcnt(0)
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: write_ds_sub_max_offset_global_clamp_bit:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    s_load_dword s0, s[4:5], 0x0
; GFX10-NEXT:    s_mov_b32 vcc_lo, 0
; GFX10-NEXT:    v_mov_b32_e32 v0, 0
; GFX10-NEXT:    v_mov_b32_e32 v2, 0x7b
; GFX10-NEXT:    v_mov_b32_e32 v3, 0
; GFX10-NEXT:    v_mov_b32_e32 v1, 0
; GFX10-NEXT:    ds_write_b32 v3, v2
; GFX10-NEXT:    s_waitcnt lgkmcnt(0)
; GFX10-NEXT:    v_div_fmas_f32 v4, s0, s0, s0
; GFX10-NEXT:    global_store_dword v[0:1], v4, off
; GFX10-NEXT:    s_waitcnt_vscnt null, 0x0
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: write_ds_sub_max_offset_global_clamp_bit:
; GFX11:       ; %bb.0:
; GFX11-NEXT:    s_load_b32 s0, s[4:5], 0x0
; GFX11-NEXT:    s_mov_b32 vcc_lo, 0
; GFX11-NEXT:    v_mov_b32_e32 v0, 0
; GFX11-NEXT:    v_dual_mov_b32 v2, 0x7b :: v_dual_mov_b32 v3, 0
; GFX11-NEXT:    v_mov_b32_e32 v1, 0
; GFX11-NEXT:    ds_store_b32 v3, v2
; GFX11-NEXT:    s_waitcnt lgkmcnt(0)
; GFX11-NEXT:    v_div_fmas_f32 v4, s0, s0, s0
; GFX11-NEXT:    global_store_b32 v[0:1], v4, off dlc
; GFX11-NEXT:    s_waitcnt_vscnt null, 0x0
; GFX11-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #1
  %sub1 = sub i32 -1, %x.i
  %tmp0 = getelementptr [256 x i32], ptr addrspace(3) @lds.obj, i32 0, i32 %sub1
  %arrayidx = getelementptr inbounds i32, ptr addrspace(3) %tmp0, i32 16383
  store i32 123, ptr addrspace(3) %arrayidx
  %fmas = call float @llvm.amdgcn.div.fmas.f32(float %dummy.val, float %dummy.val, float %dummy.val, i1 false)
  store volatile float %fmas, ptr addrspace(1) null
  ret void
}

define amdgpu_kernel void @add_x_shl_max_offset() #1 {
; CI-LABEL: add_x_shl_max_offset:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 4, v0
; CI-NEXT:    v_mov_b32_e32 v1, 13
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b8 v0, v1 offset:65535
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_max_offset:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 4, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 13
; GFX9-NEXT:    ds_write_b8 v0, v1 offset:65535
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_max_offset:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 4, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 13
; GFX10-NEXT:    ds_write_b8 v0, v1 offset:65535
; GFX10-NEXT:    s_endpgm
;
; GFX11-TRUE16-LABEL: add_x_shl_max_offset:
; GFX11-TRUE16:       ; %bb.0:
; GFX11-TRUE16-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-TRUE16-NEXT:    s_delay_alu instid0(VALU_DEP_1)
; GFX11-TRUE16-NEXT:    v_lshlrev_b32_e32 v1, 4, v0
; GFX11-TRUE16-NEXT:    v_mov_b16_e32 v0.l, 13
; GFX11-TRUE16-NEXT:    ds_store_b8 v1, v0 offset:65535
; GFX11-TRUE16-NEXT:    s_endpgm
;
; GFX11-FAKE16-LABEL: add_x_shl_max_offset:
; GFX11-FAKE16:       ; %bb.0:
; GFX11-FAKE16-NEXT:    v_dual_mov_b32 v1, 13 :: v_dual_and_b32 v0, 0x3ff, v0
; GFX11-FAKE16-NEXT:    s_delay_alu instid0(VALU_DEP_1)
; GFX11-FAKE16-NEXT:    v_lshlrev_b32_e32 v0, 4, v0
; GFX11-FAKE16-NEXT:    ds_store_b8 v0, v1 offset:65535
; GFX11-FAKE16-NEXT:    s_endpgm
  %x.i = tail call i32 @llvm.amdgcn.workitem.id.x()
  %shl = shl i32 %x.i, 4
  %add = add i32 %shl, 65535
  %z = zext i32 %add to i64
  %ptr = inttoptr i64 %z to ptr addrspace(3)
  store i8 13, ptr addrspace(3) %ptr, align 1
  ret void
}

; this could have the offset transform, but sub became xor

define amdgpu_kernel void @add_x_shl_neg_to_sub_max_offset_alt() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_max_offset_alt:
; CI:       ; %bb.0:
; CI-NEXT:    v_mul_i32_i24_e32 v0, -4, v0
; CI-NEXT:    v_mov_b32_e32 v1, 13
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b8 v0, v1 offset:65535
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_max_offset_alt:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_mul_i32_i24_e32 v0, -4, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 13
; GFX9-NEXT:    ds_write_b8 v0, v1 offset:65535
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_max_offset_alt:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_mul_i32_i24_e32 v0, -4, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 13
; GFX10-NEXT:    ds_write_b8 v0, v1 offset:65535
; GFX10-NEXT:    s_endpgm
;
; GFX11-TRUE16-LABEL: add_x_shl_neg_to_sub_max_offset_alt:
; GFX11-TRUE16:       ; %bb.0:
; GFX11-TRUE16-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-TRUE16-NEXT:    s_delay_alu instid0(VALU_DEP_1)
; GFX11-TRUE16-NEXT:    v_mul_i32_i24_e32 v1, -4, v0
; GFX11-TRUE16-NEXT:    v_mov_b16_e32 v0.l, 13
; GFX11-TRUE16-NEXT:    ds_store_b8 v1, v0 offset:65535
; GFX11-TRUE16-NEXT:    s_endpgm
;
; GFX11-FAKE16-LABEL: add_x_shl_neg_to_sub_max_offset_alt:
; GFX11-FAKE16:       ; %bb.0:
; GFX11-FAKE16-NEXT:    v_dual_mov_b32 v1, 13 :: v_dual_and_b32 v0, 0x3ff, v0
; GFX11-FAKE16-NEXT:    s_delay_alu instid0(VALU_DEP_1)
; GFX11-FAKE16-NEXT:    v_mul_i32_i24_e32 v0, -4, v0
; GFX11-FAKE16-NEXT:    ds_store_b8 v0, v1 offset:65535
; GFX11-FAKE16-NEXT:    s_endpgm
  %x.i = tail call i32 @llvm.amdgcn.workitem.id.x()
  %.neg = mul i32 %x.i, -4
  %add = add i32 %.neg, 65535
  %z = zext i32 %add to i64
  %ptr = inttoptr i64 %z to ptr addrspace(3)
  store i8 13, ptr addrspace(3) %ptr, align 1
  ret void
}

; this could have the offset transform, but sub became xor

define amdgpu_kernel void @add_x_shl_neg_to_sub_max_offset_not_canonical() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_max_offset_not_canonical:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_xor_b32_e32 v0, 0xffff, v0
; CI-NEXT:    v_mov_b32_e32 v1, 13
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b8 v0, v1
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_max_offset_not_canonical:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_xor_b32_e32 v0, 0xffff, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 13
; GFX9-NEXT:    ds_write_b8 v0, v1
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_max_offset_not_canonical:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 13
; GFX10-NEXT:    v_xor_b32_e32 v0, 0xffff, v0
; GFX10-NEXT:    ds_write_b8 v0, v1
; GFX10-NEXT:    s_endpgm
;
; GFX11-TRUE16-LABEL: add_x_shl_neg_to_sub_max_offset_not_canonical:
; GFX11-TRUE16:       ; %bb.0:
; GFX11-TRUE16-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-TRUE16-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-TRUE16-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-TRUE16-NEXT:    v_xor_b32_e32 v1, 0xffff, v0
; GFX11-TRUE16-NEXT:    v_mov_b16_e32 v0.l, 13
; GFX11-TRUE16-NEXT:    ds_store_b8 v1, v0
; GFX11-TRUE16-NEXT:    s_endpgm
;
; GFX11-FAKE16-LABEL: add_x_shl_neg_to_sub_max_offset_not_canonical:
; GFX11-FAKE16:       ; %bb.0:
; GFX11-FAKE16-NEXT:    v_dual_mov_b32 v1, 13 :: v_dual_and_b32 v0, 0x3ff, v0
; GFX11-FAKE16-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-FAKE16-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-FAKE16-NEXT:    v_xor_b32_e32 v0, 0xffff, v0
; GFX11-FAKE16-NEXT:    ds_store_b8 v0, v1
; GFX11-FAKE16-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add = add i32 65535, %shl
  %ptr = inttoptr i32 %add to ptr addrspace(3)
  store i8 13, ptr addrspace(3) %ptr
  ret void
}

define amdgpu_kernel void @add_x_shl_neg_to_sub_max_offset_p1() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_max_offset_p1:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0x10000, v0
; CI-NEXT:    v_mov_b32_e32 v1, 13
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b8 v0, v1
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_max_offset_p1:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v0, 0x10000, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 13
; GFX9-NEXT:    ds_write_b8 v0, v1
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_max_offset_p1:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 13
; GFX10-NEXT:    v_sub_nc_u32_e32 v0, 0x10000, v0
; GFX10-NEXT:    ds_write_b8 v0, v1
; GFX10-NEXT:    s_endpgm
;
; GFX11-TRUE16-LABEL: add_x_shl_neg_to_sub_max_offset_p1:
; GFX11-TRUE16:       ; %bb.0:
; GFX11-TRUE16-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-TRUE16-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-TRUE16-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-TRUE16-NEXT:    v_sub_nc_u32_e32 v1, 0x10000, v0
; GFX11-TRUE16-NEXT:    v_mov_b16_e32 v0.l, 13
; GFX11-TRUE16-NEXT:    ds_store_b8 v1, v0
; GFX11-TRUE16-NEXT:    s_endpgm
;
; GFX11-FAKE16-LABEL: add_x_shl_neg_to_sub_max_offset_p1:
; GFX11-FAKE16:       ; %bb.0:
; GFX11-FAKE16-NEXT:    v_dual_mov_b32 v1, 13 :: v_dual_and_b32 v0, 0x3ff, v0
; GFX11-FAKE16-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-FAKE16-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-FAKE16-NEXT:    v_sub_nc_u32_e32 v0, 0x10000, v0
; GFX11-FAKE16-NEXT:    ds_store_b8 v0, v1
; GFX11-FAKE16-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add = add i32 65536, %shl
  %ptr = inttoptr i32 %add to ptr addrspace(3)
  store i8 13, ptr addrspace(3) %ptr
  ret void
}

define amdgpu_kernel void @add_x_shl_neg_to_sub_multi_use() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_multi_use:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0, v0
; CI-NEXT:    v_mov_b32_e32 v1, 13
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b32 v0, v1 offset:123
; CI-NEXT:    ds_write_b32 v0, v1 offset:456
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_multi_use:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v0, 0, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 13
; GFX9-NEXT:    ds_write_b32 v0, v1 offset:123
; GFX9-NEXT:    ds_write_b32 v0, v1 offset:456
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_multi_use:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 13
; GFX10-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX10-NEXT:    ds_write_b32 v0, v1 offset:123
; GFX10-NEXT:    ds_write_b32 v0, v1 offset:456
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: add_x_shl_neg_to_sub_multi_use:
; GFX11:       ; %bb.0:
; GFX11-NEXT:    v_dual_mov_b32 v1, 13 :: v_dual_lshlrev_b32 v0, 2, v0
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_and_b32_e32 v0, 0xffc, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX11-NEXT:    ds_store_b32 v0, v1 offset:123
; GFX11-NEXT:    ds_store_b32 v0, v1 offset:456
; GFX11-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add0 = add i32 123, %shl
  %add1 = add i32 456, %shl
  %ptr0 = inttoptr i32 %add0 to ptr addrspace(3)
  store volatile i32 13, ptr addrspace(3) %ptr0
  %ptr1 = inttoptr i32 %add1 to ptr addrspace(3)
  store volatile i32 13, ptr addrspace(3) %ptr1
  ret void
}

define amdgpu_kernel void @add_x_shl_neg_to_sub_multi_use_same_offset() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_multi_use_same_offset:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0, v0
; CI-NEXT:    v_mov_b32_e32 v1, 13
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write_b32 v0, v1 offset:123
; CI-NEXT:    ds_write_b32 v0, v1 offset:123
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_multi_use_same_offset:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v0, 0, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 13
; GFX9-NEXT:    ds_write_b32 v0, v1 offset:123
; GFX9-NEXT:    ds_write_b32 v0, v1 offset:123
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_multi_use_same_offset:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 13
; GFX10-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX10-NEXT:    ds_write_b32 v0, v1 offset:123
; GFX10-NEXT:    ds_write_b32 v0, v1 offset:123
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: add_x_shl_neg_to_sub_multi_use_same_offset:
; GFX11:       ; %bb.0:
; GFX11-NEXT:    v_dual_mov_b32 v1, 13 :: v_dual_and_b32 v0, 0x3ff, v0
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_1) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX11-NEXT:    ds_store_b32 v0, v1 offset:123
; GFX11-NEXT:    ds_store_b32 v0, v1 offset:123
; GFX11-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add = add i32 123, %shl
  %ptr = inttoptr i32 %add to ptr addrspace(3)
  store volatile i32 13, ptr addrspace(3) %ptr
  store volatile i32 13, ptr addrspace(3) %ptr
  ret void
}

define amdgpu_kernel void @add_x_shl_neg_to_sub_misaligned_i64_max_offset() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0x3fb, v0
; CI-NEXT:    v_mov_b32_e32 v1, 0x7b
; CI-NEXT:    v_mov_b32_e32 v2, 0
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write2_b32 v0, v1, v2 offset1:1
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v0, 0x3fb, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 0x7b
; GFX9-NEXT:    v_mov_b32_e32 v2, 0
; GFX9-NEXT:    ds_write2_b32 v0, v1, v2 offset1:1
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 0
; GFX10-NEXT:    v_mov_b32_e32 v2, 0x7b
; GFX10-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX10-NEXT:    ds_write_b32 v0, v1 offset:1023
; GFX10-NEXT:    ds_write_b32 v0, v2 offset:1019
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset:
; GFX11:       ; %bb.0:
; GFX11-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-NEXT:    v_dual_mov_b32 v2, 0 :: v_dual_mov_b32 v1, 0x7b
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_2) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v0, 0x3fb, v0
; GFX11-NEXT:    ds_store_2addr_b32 v0, v1, v2 offset1:1
; GFX11-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add = add i32 1019, %shl
  %ptr = inttoptr i32 %add to ptr addrspace(3)
  store i64 123, ptr addrspace(3) %ptr, align 4
  ret void
}

define amdgpu_kernel void @add_x_shl_neg_to_sub_misaligned_i64_max_offset_clamp_bit(float %dummy.val) #1 {
; CI-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_clamp_bit:
; CI:       ; %bb.0:
; CI-NEXT:    s_load_dword s0, s[4:5], 0x0
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0x3fb, v0
; CI-NEXT:    s_mov_b64 vcc, 0
; CI-NEXT:    s_waitcnt lgkmcnt(0)
; CI-NEXT:    v_mov_b32_e32 v1, s0
; CI-NEXT:    v_mov_b32_e32 v2, 0x7b
; CI-NEXT:    v_div_fmas_f32 v1, v1, v1, v1
; CI-NEXT:    v_mov_b32_e32 v3, 0
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    s_mov_b64 s[0:1], 0
; CI-NEXT:    s_mov_b32 s3, 0xf000
; CI-NEXT:    s_mov_b32 s2, -1
; CI-NEXT:    ds_write2_b32 v0, v2, v3 offset1:1
; CI-NEXT:    buffer_store_dword v1, off, s[0:3], 0
; CI-NEXT:    s_waitcnt vmcnt(0)
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_clamp_bit:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    s_load_dword s0, s[4:5], 0x0
; GFX9-NEXT:    s_mov_b64 vcc, 0
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v3, 0x3fb, v0
; GFX9-NEXT:    v_mov_b32_e32 v4, 0x7b
; GFX9-NEXT:    s_waitcnt lgkmcnt(0)
; GFX9-NEXT:    v_mov_b32_e32 v1, s0
; GFX9-NEXT:    v_div_fmas_f32 v2, v1, v1, v1
; GFX9-NEXT:    v_mov_b32_e32 v0, 0
; GFX9-NEXT:    v_mov_b32_e32 v5, 0
; GFX9-NEXT:    v_mov_b32_e32 v1, 0
; GFX9-NEXT:    ds_write2_b32 v3, v4, v5 offset1:1
; GFX9-NEXT:    global_store_dword v[0:1], v2, off
; GFX9-NEXT:    s_waitcnt vmcnt(0)
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_clamp_bit:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    s_load_dword s0, s[4:5], 0x0
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    s_mov_b32 vcc_lo, 0
; GFX10-NEXT:    v_mov_b32_e32 v3, 0
; GFX10-NEXT:    v_mov_b32_e32 v4, 0x7b
; GFX10-NEXT:    v_sub_nc_u32_e32 v2, 0, v0
; GFX10-NEXT:    v_mov_b32_e32 v0, 0
; GFX10-NEXT:    v_mov_b32_e32 v1, 0
; GFX10-NEXT:    ds_write_b32 v2, v3 offset:1023
; GFX10-NEXT:    ds_write_b32 v2, v4 offset:1019
; GFX10-NEXT:    s_waitcnt lgkmcnt(0)
; GFX10-NEXT:    v_div_fmas_f32 v5, s0, s0, s0
; GFX10-NEXT:    global_store_dword v[0:1], v5, off
; GFX10-NEXT:    s_waitcnt_vscnt null, 0x0
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_clamp_bit:
; GFX11:       ; %bb.0:
; GFX11-NEXT:    s_load_b32 s0, s[4:5], 0x0
; GFX11-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-NEXT:    s_mov_b32 vcc_lo, 0
; GFX11-NEXT:    v_dual_mov_b32 v4, 0 :: v_dual_mov_b32 v3, 0x7b
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_2) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v2, 0x3fb, v0
; GFX11-NEXT:    v_mov_b32_e32 v0, 0
; GFX11-NEXT:    v_mov_b32_e32 v1, 0
; GFX11-NEXT:    ds_store_2addr_b32 v2, v3, v4 offset1:1
; GFX11-NEXT:    s_waitcnt lgkmcnt(0)
; GFX11-NEXT:    v_div_fmas_f32 v5, s0, s0, s0
; GFX11-NEXT:    global_store_b32 v[0:1], v5, off dlc
; GFX11-NEXT:    s_waitcnt_vscnt null, 0x0
; GFX11-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add = add i32 1019, %shl
  %ptr = inttoptr i32 %add to ptr addrspace(3)
  store i64 123, ptr addrspace(3) %ptr, align 4
  %fmas = call float @llvm.amdgcn.div.fmas.f32(float %dummy.val, float %dummy.val, float %dummy.val, i1 false)
  store volatile float %fmas, ptr addrspace(1) null
  ret void
}

define amdgpu_kernel void @add_x_shl_neg_to_sub_misaligned_i64_max_offset_p1() #1 {
; CI-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_p1:
; CI:       ; %bb.0:
; CI-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; CI-NEXT:    v_sub_i32_e32 v0, vcc, 0x3fc, v0
; CI-NEXT:    v_mov_b32_e32 v1, 0x7b
; CI-NEXT:    v_mov_b32_e32 v2, 0
; CI-NEXT:    s_mov_b32 m0, -1
; CI-NEXT:    ds_write2_b32 v0, v1, v2 offset1:1
; CI-NEXT:    s_endpgm
;
; GFX9-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_p1:
; GFX9:       ; %bb.0:
; GFX9-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX9-NEXT:    v_sub_u32_e32 v0, 0x3fc, v0
; GFX9-NEXT:    v_mov_b32_e32 v1, 0x7b
; GFX9-NEXT:    v_mov_b32_e32 v2, 0
; GFX9-NEXT:    ds_write2_b32 v0, v1, v2 offset1:1
; GFX9-NEXT:    s_endpgm
;
; GFX10-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_p1:
; GFX10:       ; %bb.0:
; GFX10-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX10-NEXT:    v_mov_b32_e32 v1, 0
; GFX10-NEXT:    v_mov_b32_e32 v2, 0x7b
; GFX10-NEXT:    v_sub_nc_u32_e32 v0, 0, v0
; GFX10-NEXT:    v_add_nc_u32_e32 v0, 0x200, v0
; GFX10-NEXT:    ds_write2_b32 v0, v2, v1 offset0:127 offset1:128
; GFX10-NEXT:    s_endpgm
;
; GFX11-LABEL: add_x_shl_neg_to_sub_misaligned_i64_max_offset_p1:
; GFX11:       ; %bb.0:
; GFX11-NEXT:    v_and_b32_e32 v0, 0x3ff, v0
; GFX11-NEXT:    v_dual_mov_b32 v2, 0 :: v_dual_mov_b32 v1, 0x7b
; GFX11-NEXT:    s_delay_alu instid0(VALU_DEP_2) | instskip(NEXT) | instid1(VALU_DEP_1)
; GFX11-NEXT:    v_lshlrev_b32_e32 v0, 2, v0
; GFX11-NEXT:    v_sub_nc_u32_e32 v0, 0x3fc, v0
; GFX11-NEXT:    ds_store_2addr_b32 v0, v1, v2 offset1:1
; GFX11-NEXT:    s_endpgm
  %x.i = call i32 @llvm.amdgcn.workitem.id.x() #0
  %neg = sub i32 0, %x.i
  %shl = shl i32 %neg, 2
  %add = add i32 1020, %shl
  %ptr = inttoptr i32 %add to ptr addrspace(3)
  store i64 123, ptr addrspace(3) %ptr, align 4
  ret void
}

declare float @llvm.amdgcn.div.fmas.f32(float, float, float, i1)

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind convergent }
