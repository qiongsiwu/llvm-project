//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <clc/opencl/opencl-base.h>
#include <clc/utils.h>

#define __CLC_ATOM_IMPL(AS, TYPE)                                              \
  _CLC_OVERLOAD _CLC_DEF TYPE __CLC_XCONCAT(atom_, __CLC_ATOMIC_OP)(           \
      volatile AS TYPE * p, TYPE val) {                                        \
    return __CLC_XCONCAT(atomic_, __CLC_ATOMIC_OP)(p, val);                    \
  }

__CLC_ATOM_IMPL(__CLC_ATOMIC_ADDRESS_SPACE, int)
__CLC_ATOM_IMPL(__CLC_ATOMIC_ADDRESS_SPACE, uint)

#undef __CLC_ATOM_IMPL
#undef __CLC_ATOMIC_OP
#undef __CLC_ATOMIC_ADDRESS_SPACE
