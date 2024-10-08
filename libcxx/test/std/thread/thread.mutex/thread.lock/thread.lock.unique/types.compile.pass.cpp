//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// <mutex>

// template <class Mutex>
// class unique_lock
// {
// public:
//     typedef Mutex mutex_type;
//     ...
// };

#include <mutex>
#include <type_traits>

#include "checking_mutex.h"

static_assert(std::is_same<std::unique_lock<checking_mutex>::mutex_type, checking_mutex>::value, "");
