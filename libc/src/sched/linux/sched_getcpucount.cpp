//===-- Implementation of sched_getcpucount -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sched/sched_getcpucount.h"

#include "src/__support/CPP/bit.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include "hdr/types/cpu_set_t.h"
#include "hdr/types/size_t.h"
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, __sched_getcpucount,
                   (size_t cpuset_size, const cpu_set_t *mask)) {
  int result = 0;
  for (size_t i = 0; i < cpuset_size / sizeof(long); ++i) {
    result += cpp::popcount(mask->__mask[i]);
  }
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
