
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// <unordered_map>

// Check that std::unordered_multimap and its iterators can be instantiated with an incomplete
// type.

#include <unordered_map>

#include "min_allocator.h"
#include "test_macros.h"

template <class Tp>
struct MyHash {
  MyHash() {}
  std::size_t operator()(Tp const&) const { return 42; }
};

struct A {
  typedef std::unordered_multimap<A, A, MyHash<A> > Map;
  Map m;
  Map::iterator it;
  Map::const_iterator cit;
  Map::local_iterator lit;
  Map::const_local_iterator clit;
};

inline bool operator==(A const& L, A const& R) { return &L == &R; }

int main(int, char**) {
  A a;

  // Make sure that the allocator isn't rebound to an incomplete type
  std::unordered_multimap<int,
                          int,
                          std::hash<int>,
                          std::equal_to<int>,
                          complete_type_allocator<std::pair<const int, int> > >
      m;

  return 0;
}
