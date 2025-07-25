//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <clc/opencl/workitem/get_global_size.h>
#include <clc/opencl/workitem/get_local_size.h>
#include <clc/opencl/workitem/get_num_groups.h>

_CLC_DEF _CLC_OVERLOAD size_t get_global_size(uint dim) {
  return get_num_groups(dim) * get_local_size(dim);
}
