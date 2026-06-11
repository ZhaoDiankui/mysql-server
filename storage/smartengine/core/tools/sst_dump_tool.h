/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
#pragma once

#include "storage/io_extent.h"
#include "table/extent_struct.h"
namespace smartengine
{
namespace table
{
struct RowBlock;
} // namespace table
namespace tools
{
class ExtentDumper
{
public:
  ExtentDumper() : internal_comparator_(util::BytewiseComparator()), extent_() {}
  ~ExtentDumper() {}

  int init(const std::string &file_path, const int32_t offset);
  int dump();

private:
  int read_footer(table::Footer &footer);
  int dump_footer(const table::Footer &footer);
  int dump_index_block(table::RowBlock *index_block);
  int dump_all_data_block(table::RowBlock *index_block);
  int dump_data_block(const table::BlockInfo &block_info);
  int summry(const table::Footer &footer, table::RowBlock *index_block);
  int read_block(const table::BlockHandle &handle, table::RowBlock *&block);

private:
  db::InternalKeyComparator internal_comparator_;
  storage::FileIOExtent extent_;
};

class SSTDumpTool {
public:
  int Run(int argc, char** argv);

private:
  void print_help();
};

}  // namespace tools
}  // namespace smartengine
