/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
#pragma once
#include <string>
#include <vector>
#include "db/db.h"
#include "options/options.h"

namespace smartengine {
namespace tools {

// An interface for converting a slice to a readable string
class SliceFormatter {
 public:
  virtual ~SliceFormatter() {}
  virtual std::string Format(const common::Slice& s) const = 0;
};

// common::Options for customizing ldb tool (beyond the DB Options)
struct LDBOptions {
  // Create LDBOptions with default values for all fields
  LDBOptions();

  // Key formatter that converts a slice to a readable string.
  // Default: Slice::ToString()
  std::shared_ptr<SliceFormatter> key_formatter;

  std::string print_help_header = "ldb - RocksDB Tool";
};

class LDBTool {
 public:
  void Run(
      int argc, char** argv, common::Options db_options = common::Options(),
      const LDBOptions& ldb_options = LDBOptions(),
      const std::vector<db::ColumnFamilyDescriptor>* column_families = nullptr);
};

}  // namespace tools
}  // namespace smartengine