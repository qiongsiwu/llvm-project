//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CXX03___TYPE_TRAITS_RANK_H
#define _LIBCPP___CXX03___TYPE_TRAITS_RANK_H

#include <__cxx03/__config>
#include <__cxx03/__type_traits/integral_constant.h>
#include <__cxx03/cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// TODO: Enable using the builtin __array_rank when https://llvm.org/PR57133 is resolved
#if __has_builtin(__array_rank) && 0

template <class _Tp>
struct rank : integral_constant<size_t, __array_rank(_Tp)> {};

#else

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS rank : public integral_constant<size_t, 0> {};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS rank<_Tp[]> : public integral_constant<size_t, rank<_Tp>::value + 1> {};
template <class _Tp, size_t _Np>
struct _LIBCPP_TEMPLATE_VIS rank<_Tp[_Np]> : public integral_constant<size_t, rank<_Tp>::value + 1> {};

#endif // __has_builtin(__array_rank)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CXX03___TYPE_TRAITS_RANK_H
