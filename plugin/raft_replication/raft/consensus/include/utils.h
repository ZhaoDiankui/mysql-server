// Copyright (c) 2023 ApeCloud, Inc.

#pragma once

namespace alisql {

template <typename Function>
class Defer {
 public:
  Defer(Function func) : func_(func) {}
  ~Defer() { func_(); }
  Function func_;
};

}  // namespace alisql