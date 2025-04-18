//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, c++11, c++14, c++17, c++20

// <flat_set>

// size_type erase(const key_type& k);

#include <compare>
#include <concepts>
#include <deque>
#include <flat_set>
#include <functional>
#include <utility>
#include <vector>

#include "MinSequenceContainer.h"
#include "../helpers.h"
#include "test_macros.h"
#include "min_allocator.h"

template <class KeyContainer, class Compare = std::less<>>
void test_one() {
  using M = std::flat_multiset<int, Compare, KeyContainer>;

  auto make = [](std::initializer_list<int> il) {
    M m;
    for (int i : il) {
      m.emplace(i);
    }
    return m;
  };
  M m = make({1, 1, 3, 5, 5, 5, 7, 8});
  ASSERT_SAME_TYPE(decltype(m.erase(9)), typename M::size_type);
  auto n = m.erase(9);
  assert(n == 0);
  assert(m == make({1, 1, 3, 5, 5, 5, 7, 8}));
  n = m.erase(4);
  assert(n == 0);
  assert(m == make({1, 1, 3, 5, 5, 5, 7, 8}));
  n = m.erase(1);
  assert(n == 2);
  assert(m == make({3, 5, 5, 5, 7, 8}));
  n = m.erase(8);
  assert(n == 1);
  assert(m == make({3, 5, 5, 5, 7}));
  n = m.erase(3);
  assert(n == 1);
  assert(m == make({5, 5, 5, 7}));
  n = m.erase(4);
  assert(n == 0);
  assert(m == make({5, 5, 5, 7}));
  n = m.erase(6);
  assert(n == 0);
  assert(m == make({5, 5, 5, 7}));
  n = m.erase(7);
  assert(n == 1);
  assert(m == make({5, 5, 5}));
  n = m.erase(2);
  assert(n == 0);
  assert(m == make({5, 5, 5}));
  n = m.erase(5);
  assert(n == 3);
  assert(m.empty());
  // was empty
  n = m.erase(5);
  assert(n == 0);
  assert(m.empty());
}

void test() {
  test_one<std::vector<int>>();
  test_one<std::vector<int>, std::greater<>>();
  test_one<std::deque<int>>();
  test_one<MinSequenceContainer<int>>();
  test_one<std::vector<int, min_allocator<int>>>();
}

void test_exception() {
  auto erase_function = [](auto& m, auto key_arg) {
    using Set = std::decay_t<decltype(m)>;
    using Key = typename Set::key_type;
    const Key key{key_arg};
    m.erase(key);
  };
  test_erase_exception_guarantee(erase_function);
}

int main(int, char**) {
  test();
  test_exception();

  return 0;
}
