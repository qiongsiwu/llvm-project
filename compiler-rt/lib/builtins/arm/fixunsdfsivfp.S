//===-- fixunsdfsivfp.S - Implement fixunsdfsivfp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern unsigned int __fixunsdfsivfp(double a);
//
// Converts double precision float to a 32-bit unsigned int rounding towards
// zero. All negative values become zero.
// Uses Darwin calling convention where a double precision parameter is
// passed in GPR register pair.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__fixunsdfsivfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vcvt.u32.f64 s0, d0
	vmov r0, s0
#else
	VMOV_TO_DOUBLE(d7, r0, r1)    // load double register from R0/R1
	vcvt.u32.f64 s15, d7  // convert double to 32-bit int into s15
	vmov	r0, s15	      // move s15 to result register
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__fixunsdfsivfp)

NO_EXEC_STACK_DIRECTIVE

