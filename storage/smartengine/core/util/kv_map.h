//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
#pragma once

#include <map>
#include <string>
#include "util/slice.h"

namespace smartengine {
namespace util {
namespace stl_wrappers {

struct LessOfComparator {
  explicit LessOfComparator(
      const util::Comparator* c = util::BytewiseComparator())
      : cmp(c) {}

  bool operator()(const std::string& a, const std::string& b) const {
    return cmp->Compare(common::Slice(a), common::Slice(b)) < 0;
  }
  bool operator()(const common::Slice& a, const common::Slice& b) const {
    return cmp->Compare(a, b) < 0;
  }

  const util::Comparator* cmp;
};

typedef std::map<std::string, std::string, LessOfComparator> KVMap;
}  // stl_wrappers
}  // util
}  // smartengine
