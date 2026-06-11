//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once

#include "port/port.h"
#include "table/internal_iterator.h"

namespace smartengine {
namespace table {
class ScopedArenaIterator {
  void reset(InternalIterator* iter) noexcept {
    if (iter_ != nullptr) {
      iter_->~InternalIterator();
    }
    iter_ = iter;
  }

 public:
  explicit ScopedArenaIterator(InternalIterator* iter = nullptr)
      : iter_(iter) {}

  ScopedArenaIterator(const ScopedArenaIterator&) = delete;
  ScopedArenaIterator& operator=(const ScopedArenaIterator&) = delete;

  ScopedArenaIterator(ScopedArenaIterator&& o) noexcept {
    iter_ = o.iter_;
    o.iter_ = nullptr;
  }

  ScopedArenaIterator& operator=(ScopedArenaIterator&& o) noexcept {
    reset(o.iter_);
    o.iter_ = nullptr;
    return *this;
  }

  InternalIterator* operator->() { return iter_; }
  InternalIterator* get() { return iter_; }

  void set(InternalIterator* iter) { reset(iter); }

  InternalIterator* release() {
    assert(iter_ != nullptr);
    auto* res = iter_;
    iter_ = nullptr;
    return res;
  }

  ~ScopedArenaIterator() { reset(nullptr); }

 private:
  InternalIterator* iter_;
};
}  // namespace table
}  // namespace smartengine
