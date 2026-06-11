//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include "options/advanced_options.h"
#include "util/slice.h"
#include "util/to_string.h"
#include <cstdint>
#include <stddef.h>
#include <stdint.h>
#ifdef ROCKSDB_MALLOC_USABLE_SIZE
#ifdef OS_FREEBSD
#include <malloc_np.h>
#else
#include <malloc.h>
#endif
#endif

#include "options/options.h"


namespace smartengine
{
namespace storage
{
class IOExtent; 
}
namespace util
{
class Comparator;
}

namespace table
{

struct RowBlock {
 public:
  RowBlock(const char *data, const int64_t data_size, const common::CompressionType compress_type);
  RowBlock(const RowBlock&) = delete;
  RowBlock(const RowBlock&&) = delete;
  RowBlock &operator=(const RowBlock&) = delete;
  RowBlock &operator=(const RowBlock&&) = delete;
  ~RowBlock();

  void destroy();
  bool is_valid() const;
  int64_t usable_size() const;
  void restarts_info(uint32_t &restarts_count, uint32_t &restarts_offset) const;

  DECLARE_TO_STRING()
public:
  static const int64_t RESTARTS_ELEMENT_SIZE = sizeof(uint32_t);

public:
  const char* data_;
  uint32_t size_;
  common::CompressionType compress_type_;
};

//TODO(Zhao Dongsheng) : BlockContents is now only needed by filter, and will deprecate in future, place here temporarily。
struct BlockContents
{
  common::Slice data;  // Actual contents of data
  bool cachable;       // True iff data can be cached
  common::CompressionType compression_type;
  // allocation.get() must be allocated by base_malloc()
  std::unique_ptr<char[], memory::ptr_delete<char>> allocation;

  BlockContents()
      : cachable(false),
        compression_type(common::CompressionType::kNoCompression) {}

  BlockContents(const common::Slice& _data, bool _cachable,
                common::CompressionType _compression_type)
      : data(_data), cachable(_cachable), compression_type(_compression_type) {}

  BlockContents(std::unique_ptr<char[], memory::ptr_delete<char>>&& _data,
                size_t _size, bool _cachable,
                common::CompressionType _compression_type)
      : data(_data.get(), _size),
        cachable(_cachable),
        compression_type(_compression_type),
        allocation(std::move(_data)) {}

  BlockContents(BlockContents&& other) noexcept {
    *this = std::move(other);
  }

  BlockContents& operator=(BlockContents&& other) {
    data = std::move(other.data);
    cachable = other.cachable;
    compression_type = other.compression_type;
    allocation = std::move(other.allocation);
    return *this;
  }
};

}  // namespace table
}  // namespace smartengine
