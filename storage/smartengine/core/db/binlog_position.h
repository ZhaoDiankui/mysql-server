/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include "util/to_string.h"
#include "util/serialization.h"

namespace smartengine
{
namespace db
{
struct BinlogPosition
{
  static const int64_t BINLOG_POSITION_VERSION = 1;

  std::string file_name_;
  uint64_t offset_;

  BinlogPosition() : file_name_(), offset_(0) {}

  BinlogPosition(const char *file_name, uint64_t offset)
      : file_name_(file_name), offset_(offset) {}

  BinlogPosition(const BinlogPosition &) = default;

  BinlogPosition(BinlogPosition &&other)
      : file_name_(std::move(other.file_name_)), offset_(other.offset_) {}

  BinlogPosition &operator=(const BinlogPosition &) = default;

  ~BinlogPosition() {}

  void set(const char *file_name, uint64_t offset) {
    if (file_name != nullptr) {
      file_name_.assign(file_name);
      offset_ = offset;
    }
  }

  bool valid() { return file_name_.length() > 0 && offset_ > 0; }

  int compare(const BinlogPosition &other) {
    if (other.file_name_.length() == 0) return 1;

    if (file_name_.length() == 0) return -1;

    int r = file_name_.compare(other.file_name_);
    if (r == 0) {
      r = offset_ - other.offset_;
    }
    return r;
  }

  void clear() {
    file_name_.clear();
    offset_ = 0;
  }

  DECLARE_TO_STRING()
  DECLARE_COMPACTIPLE_SERIALIZATION(BINLOG_POSITION_VERSION)
};

} //namespace db
} //namespace smartengine
