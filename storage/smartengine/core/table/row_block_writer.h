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

#include "table/block_writer.h"
#include <cstdint>
#include <vector>
#include "util/data_buffer.h"

namespace smartengine
{
namespace table
{

class RowBlockWriter : public BlockWriter
{
public:
  RowBlockWriter();
  RowBlockWriter(const RowBlockWriter &other);
  virtual ~RowBlockWriter() override;

  int init(const int64_t restart_interval);
  void destroy();
  virtual void reuse() override;
  virtual int append(const common::Slice &key, const common::Slice &value) override;
  virtual int build(common::Slice &block, BlockInfo &block_info) override;
  int build(util::AutoBufferWriter &dest_buf, common::Slice &block);
  virtual int64_t current_size() const override { return current_block_size_; }
  virtual int64_t future_size(const uint32_t key_size, const uint32_t value_size) const override;
  virtual bool is_empty() const override;

private:
  int append_restarts(util::AutoBufferWriter &dest_buf);

private:
  bool is_inited_;
  int64_t restart_interval_;
  std::string last_key_;
  int64_t counter_;
  int64_t current_block_size_;
  std::vector<uint32_t> restarts_;
  util::AutoBufferWriter buf_;
};

}  // namespace table
}  // namespace smartengine