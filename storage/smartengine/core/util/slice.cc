//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/slice.h"

#include <stdio.h>
#include <algorithm>
#include "memory/allocator.h"
#include "memory/page_arena.h"
#include "util/string_util.h"
#include "util/to_string.h"

namespace smartengine {
using namespace memory;

namespace common {
// 2 small internal utility functions, for efficient hex conversions
// and no need for snprintf, toupper etc...
// Originally from wdt/util/EncryptionUtils.cpp - for ToString(true)/DecodeHex:
char toHex(unsigned char v) {
  if (v <= 9) {
    return '0' + v;
  }
  return 'A' + v - 10;
}
// most of the code is for validation/error check
int fromHex(char c) {
  // toupper:
  if (c >= 'a' && c <= 'f') {
    c -= ('a' - 'A');  // aka 0x20
  }
  // validation
  if (c < '0' || (c > '9' && (c < 'A' || c > 'F'))) {
    return -1;  // invalid not 0-9A-F hex char
  }
  if (c <= '9') {
    return c - '0';
  }
  return c - 'A' + 10;
}

Slice::Slice(const SliceParts& parts, std::string* buf) {
  size_t length = 0;
  for (int i = 0; i < parts.num_parts; ++i) {
    length += parts.parts[i].size();
  }
  buf->reserve(length);

  for (int i = 0; i < parts.num_parts; ++i) {
    buf->append(parts.parts[i].data(), parts.parts[i].size());
  }
  data_ = buf->data();
  size_ = buf->size();
}

Slice Slice::deep_copy(memory::Allocator& allocator) const {
  if (nullptr == data_ || size_ <= 0) return Slice();
  char* dest = allocator.Allocate(size_);
  if (nullptr == dest) return Slice();
  memcpy(dest, data_, size_);
  return Slice(dest, size_);
}

Slice Slice::deep_copy(ArenaAllocator& allocator) const {
  if (nullptr == data_ || size_ <= 0) return Slice();
  char* dest = static_cast<char*>(allocator.alloc(size_));
  if (nullptr == dest) return Slice();
  memcpy(dest, data_, size_);
  return Slice(dest, size_);
}


Slice Slice::deep_copy(SimpleAllocator& allocator) const {
  if (nullptr == data_ || size_ <= 0) return Slice();
  char* dest = (char*)allocator.alloc(size_);
  if (nullptr == dest) return Slice();
  memcpy(dest, data_, size_);
  return Slice(dest, size_);
}

// Return a string that contains the copy of the referenced data.
std::string Slice::ToString(bool hex) const {
  std::string result;  // RVO/NRVO/move
  if (hex) {
    result.reserve(2 * size_);
    for (size_t i = 0; i < size_; ++i) {
      unsigned char c = data_[i];
      result.push_back(toHex(c >> 4));
      result.push_back(toHex(c & 0xf));
    }
    return result;
  } else {
    result.assign(data_, size_);
    return result;
  }
}

int64_t Slice::to_string(char* buf, const int64_t buf_len) const {
  int64_t  max_print_size = std::min((int64_t)128, buf_len/2);
  int64_t pos = 0;
  if ((int64_t)size_ * 2 <= max_print_size) {
    pos = util::hex_to_str(data_, size_, buf, buf_len);
  } else {
    pos = util::hex_to_str(data_, max_print_size / 2, buf, buf_len);
  }
  pos *= 2;
  if ((int64_t)size_ * 2 > max_print_size) {
    util::databuff_printf(buf, buf_len, pos, "...");
  }
  return pos;
}

// Originally from rocksdb/utilities/ldb_cmd.h
bool Slice::DecodeHex(std::string* result) const {
  std::string::size_type len = size_;
  if (len % 2) {
    // Hex string must be even number of hex digits to get complete bytes back
    return false;
  }
  if (!result) {
    return false;
  }
  result->clear();
  result->reserve(len / 2);

  for (size_t i = 0; i < len;) {
    int h1 = fromHex(data_[i++]);
    if (h1 < 0) {
      return false;
    }
    int h2 = fromHex(data_[i++]);
    if (h2 < 0) {
      return false;
    }
    result->push_back((h1 << 4) | h2);
  }
  return true;
}

}  // namespace common
}  // namespace smartengine
