; RUN: llc -mtriple=amdgcn -mcpu=verde < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -mtriple=amdgcn -mcpu=tonga < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s

; FUNC-LABEL: {{^}}v_and_i64_br:
; SI: s_and_b64
define amdgpu_kernel void @v_and_i64_br(ptr addrspace(1) %out, ptr addrspace(1) %aptr, ptr addrspace(1) %bptr) {
entry:
  %tid = call i32 @llvm.amdgcn.mbcnt.lo(i32 -1, i32 0) #0
  %tmp0 = icmp eq i32 %tid, 0
  br i1 %tmp0, label %if, label %endif

if:
  %a = load i64, ptr addrspace(1) %aptr, align 8
  %b = load i64, ptr addrspace(1) %bptr, align 8
  %and = and i64 %a, %b
  br label %endif

endif:
  %tmp1 = phi i64 [%and, %if], [0, %entry]
  store i64 %tmp1, ptr addrspace(1) %out, align 8
  ret void
}

declare i32 @llvm.amdgcn.mbcnt.lo(i32, i32) #0

attributes #0 = { nounwind readnone }
