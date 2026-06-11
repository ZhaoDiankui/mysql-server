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

#include "table/block_struct.h"
#include "table/row_block_writer.h"

namespace smartengine
{
namespace table
{

class IndexBlockWriter
{
public:
  IndexBlockWriter();
  ~IndexBlockWriter();

  int init();
  void reuse();
  int append(const common::Slice &key, const BlockInfo &block_info);
  int build(common::Slice &block);
  int future(const common::Slice &key, const BlockInfo &block_info, common::Slice &block);
  bool is_empty() const;
  int64_t future_size(const common::Slice &key, const BlockInfo &block_info) const;

private:
  int serialize_block_stats(const BlockInfo &block_info, common::Slice &serialized_value);

private:
  static const int64_t INDEX_BLOCK_RESTART_INTERVAL = 1; // row index

private:
  bool is_inited_;
  util::AutoBufferWriter block_info_buf_;
  RowBlockWriter block_writer_;
  util::AutoBufferWriter shadow_block_buf_;
  std::string future_key_;
  RowBlockWriter shadow_block_writer_;
};

}  // namespace table
}  // namespace smartengine