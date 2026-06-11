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
//
// BlockBuilder generates blocks where keys are prefix-compressed:
//
// When we store a key, we drop the prefix shared with the previous
// string.  This helps reduce the space requirement significantly.
// Furthermore, once every K keys, we do not apply the prefix
// compression and store the entire key.  We call this a "restart
// point".  The tail end of the block stores the offsets of all of the
// restart points, and can be used to do a binary search when looking
// for a particular key.  Values are stored as-is (without compression)
// immediately following the corresponding key.
//
// An entry for a particular key-value pair has the form:
//     shared_bytes: varint32
//     unshared_bytes: varint32
//     value_length: varint32
//     key_delta: char[unshared_bytes]
//     value: char[value_length]
// shared_bytes == 0 for restart points.
//
// The trailer of the block has the form:
//     restarts: uint32[num_restarts]
//     num_restarts: uint32
// restarts[i] contains the offset within the block of the ith restart point.

#include "table/row_block_writer.h"

#include <assert.h>
#include "table/block_struct.h"
#include "util/coding.h"
#include "util/serialization.h"

namespace smartengine
{
using namespace common;
using namespace memory;
using namespace util;

namespace table
{

RowBlockWriter::RowBlockWriter() : is_inited_(false),
                                   restart_interval_(0),
                                   last_key_(),
                                   counter_(0),
                                   current_block_size_(0),
                                   restarts_(),
                                   buf_(ModId::kRowBlockWriter)
{}

RowBlockWriter::~RowBlockWriter() {}

int RowBlockWriter::init(const int64_t restart_interval)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "RowBlockWriter has been initialized", K(ret));
  } else if (UNLIKELY(restart_interval <=0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(restart_interval));
  } else {
    restart_interval_ = restart_interval;
    restarts_.push_back(0);
    is_inited_ = true;
  }

  return ret;
}

void RowBlockWriter::reuse()
{
  last_key_.clear();
  counter_ = 0;
  current_block_size_ = 0;
  restarts_.clear();
  restarts_.push_back(0);
  buf_.reuse();
}

int RowBlockWriter::append(const Slice &key, const Slice &value)
{
  int ret = Status::kOk;
  uint32_t shared = 0;
  uint32_t non_shared = key.size();
  // Add "<shared><non_shared><value_size>" to buf_.
  const uint32_t var_3int_size = 15;
  int64_t pos = 0;
  Slice last_key(last_key_);

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowBlockWriter should be initialized", K(ret));
  } else if (UNLIKELY(key.size() <= 8)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), "key_size", key.size());
  } else if (FAILED(buf_.reserve(var_3int_size))) {
    SE_LOG(WARN, "fail to reserve buf", K(ret), K(var_3int_size), "size", buf_.size());
  } else {
    // restart delta encoding after appending restart_interval_ data
    if (counter_ >= restart_interval_) {
      restarts_.push_back(static_cast<uint32_t>(buf_.size()));
      counter_ = 0;
    } else {
      // calculate common prefix size with previous key since second key among the restart_interval_. 
      shared = key.difference_offset(last_key);
      non_shared = key.size() - shared;
    }

    // write row header with format: {shared-size:non-shared-size:value-size}
    util::serialize(buf_.current(), var_3int_size, pos,
        shared, non_shared, value.size());
    assert(pos > 0);
    buf_.advance(pos);

    // write payloads of key and value
    if (FAILED(buf_.write(key.data() + shared, non_shared))) {
      SE_LOG(WARN, "fail to write key", K(ret), K(shared), K(non_shared));
    } else if (FAILED(buf_.write(value))) {
      SE_LOG(WARN, "fail to write value", K(ret));
    } else {
      ++counter_;
      last_key_.assign(key.data(), key.size());
      current_block_size_ = buf_.size() + restarts_.size() * sizeof(uint32_t);
    }
  }

  return ret;
}

int RowBlockWriter::build(Slice &block, BlockInfo &block_info)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowBlockWriter should be initialized", K(ret));
  } else if (UNLIKELY(buf_.size() <= 0)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "block is empty", K(ret));
  } else if (FAILED(append_restarts(buf_))) {
    SE_LOG(WARN, "fail to append restarts", K(ret));
  } else {
    block.assign(buf_.data(), buf_.size());
    block_info.set_row_format();
  }

  return ret;
}

int RowBlockWriter::build(util::AutoBufferWriter &dest_buf, Slice &block)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowBlockWriter should be initialized", K(ret));
  } else if (UNLIKELY(buf_.size() <= 0)) {
    ret = Status::kErrorUnexpected;
  } else if (FAILED(dest_buf.write(buf_.data(), buf_.size()))) {
    SE_LOG(WARN, "fail to write to dest buf", K(ret), "buf_size", buf_.size());
  } else if (FAILED(append_restarts(dest_buf))) {
    SE_LOG(WARN, "fail to append restarts", K(ret));
  } else {
    block.assign(dest_buf.data(), dest_buf.size());
  }

  return ret;
}

int64_t RowBlockWriter::future_size(const uint32_t key_size, const uint32_t value_size) const
{
  int64_t size = current_block_size_;
  
  size += sizeof(uint32_t); // shared-length size
  size += VarintLength(key_size); // non-shared-length size
  size += VarintLength(value_size); // value size
  size += (key_size + value_size); // row size

  return size;
}

bool RowBlockWriter::is_empty() const { return (0 == buf_.size()); }

int RowBlockWriter::append_restarts(AutoBufferWriter &dest_buf)
{
  int ret = Status::kOk;
  uint32_t item = 0;
  uint32_t item_count = restarts_.size();
  // items in restarts and size of restarts
  uint32_t restarts_size = item_count * sizeof(item) + sizeof(item_count);


  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowBlockWriter should be initialized", K(ret));
  } else if (UNLIKELY(restarts_.empty())) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "restarts is empty", K(ret));
  } else if (FAILED(dest_buf.reserve(restarts_size))) {
    SE_LOG(WARN, "fail to reserve buf for restarts size", K(ret), K(restarts_size));
  } else {
    for (uint32_t i = 0; i < restarts_.size(); ++i) {
      //PutFixed32(buf_.current(), val);
      item = restarts_[i];
      EncodeFixed32(dest_buf.current(), item);
      dest_buf.advance(sizeof(item));
    }
    EncodeFixed32(dest_buf.current(), item_count);
    dest_buf.advance(sizeof(item_count));
    se_assert(item_count > 0);
  }

  return ret;
}

}  // namespace table
}  // namespace smartengine
