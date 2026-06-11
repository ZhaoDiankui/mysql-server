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

#include <stdint.h>
#include <string>
#include "table/table.h"
#include "db/dbformat.h"

namespace smartengine {

namespace util {
class WritableFileWriter;
class RandomAccessFileReader;
}

namespace db {
struct MiniTables;
}

namespace common {
struct DBOptions;
class Status;
struct ColumnFamilyOptions;
}

namespace table {

class ExtentBasedTableFactory : public TableFactory {
 public:
  explicit ExtentBasedTableFactory(
      const BlockBasedTableOptions& table_options = BlockBasedTableOptions());

  ~ExtentBasedTableFactory() override {}

  const char* Name() const override { return "ExtentBasedTable"; }

  // Sanitizes the specified DB Options.
  common::Status SanitizeOptions(
      const common::DBOptions& db_opts,
      const common::ColumnFamilyOptions& cf_opts) const override;

  std::string GetPrintableTableOptions() const override;

  const BlockBasedTableOptions& table_options() const;

  void* GetOptions() override { return &table_options_; }

 private:
  BlockBasedTableOptions table_options_;
};

}  // namespace table
}  // namespace smartengine
