//===-- Linux implementation of isatty ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/isatty.h"

#include "src/__support/OSUtil/syscall.h" // For internal syscall function.
#include "src/__support/common.h"

#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include <sys/ioctl.h>   // For ioctl numbers.
#include <sys/syscall.h> // For syscall numbers.

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, isatty, (int fd)) {
  constexpr int INIT_VAL = 0x1234abcd;
  int line_d_val = INIT_VAL;
  // This gets the line dicipline of the terminal. When called on something that
  // isn't a terminal it doesn't change line_d_val and returns -1.
  int result =
      LIBC_NAMESPACE::syscall_impl<int>(SYS_ioctl, fd, TIOCGETD, &line_d_val);
  if (result == 0)
    return 1;

  libc_errno = -result;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
