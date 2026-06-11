/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <vector>

#include "db/dbformat.h"
#include "options/options.h"

namespace smartengine {

namespace table {
class FilterManager;
}

namespace common {

// ImmutableCFOptions is a data struct used by smartengine internal. It contains a
// subset of Options that should not be changed during the entire lifetime
// of DB. Raw pointers defined in this struct do not have ownership to the data
// they point to. Options contains shared_ptr to these data.
struct ImmutableCFOptions {
  ImmutableCFOptions() = delete;
  explicit ImmutableCFOptions(const Options& options);
  ImmutableCFOptions(const ImmutableDBOptions& db_options,
                     const ColumnFamilyOptions& cf_options);

  void dump() const;

  const util::Comparator* user_comparator;
  int min_write_buffer_number_to_merge;
  int max_write_buffer_number_to_maintain;
  monitor::Statistics* statistics;
  util::RateLimiter* rate_limiter;
  util::Env* env;
  std::vector<DbPath> db_paths;
  memtable::MemTableRepFactory* memtable_factory;
  table::TableFactory* table_factory;
  std::vector<CompressionType> compression_per_level;
  CompressionOptions compression_opts;
  bool level_compaction_dynamic_level_bytes;
  //TODO(Zhao Dongsheng), Putting the member variable "row cache" here is not appropriate.
  std::shared_ptr<cache::RowCache> row_cache;
};

struct MutableCFOptions {
  explicit MutableCFOptions(const ColumnFamilyOptions& options)
      : write_buffer_size(options.write_buffer_size),
        flush_delete_percent(options.flush_delete_percent),
        compaction_delete_percent(options.compaction_delete_percent),
        flush_delete_percent_trigger(options.flush_delete_percent_trigger),
        flush_delete_record_trigger(options.flush_delete_record_trigger),
        disable_auto_compactions(options.disable_auto_compactions),
        level0_file_num_compaction_trigger(
            options.level0_file_num_compaction_trigger),
        level0_layer_num_compaction_trigger(
            options.level0_layer_num_compaction_trigger),
        level1_extents_major_compaction_trigger(
            options.level1_extents_major_compaction_trigger),
        level2_usage_percent(options.level2_usage_percent),
        background_disable_merge(false),
        scan_add_blocks_limit(options.scan_add_blocks_limit),
        bottommost_level(options.bottommost_level),
        compaction_task_extents_limit(options.compaction_task_extents_limit) {}

  MutableCFOptions()
      : write_buffer_size(0),
        flush_delete_percent(100),
        compaction_delete_percent(100),
        flush_delete_percent_trigger(700000),
        flush_delete_record_trigger(700000),
        disable_auto_compactions(false),
        level0_file_num_compaction_trigger(0),
        level0_layer_num_compaction_trigger(0),
        level1_extents_major_compaction_trigger(0),
        level2_usage_percent(0),
        background_disable_merge(false),
        scan_add_blocks_limit(0),
        bottommost_level(2),
        compaction_task_extents_limit(512) {}

  void Dump() const;

  // Memtable related options
  size_t write_buffer_size;
  int flush_delete_percent;
  int compaction_delete_percent;
  int flush_delete_percent_trigger;
  int flush_delete_record_trigger;

  // Compaction related options
  bool disable_auto_compactions;
  int level0_file_num_compaction_trigger;
  int level0_layer_num_compaction_trigger;
  int level1_extents_major_compaction_trigger;
  int64_t level2_usage_percent;

  // Misc options
  bool background_disable_merge;

  uint64_t scan_add_blocks_limit;
  int bottommost_level;
  int compaction_task_extents_limit;
};

}  // namespace common
}  // namespace smartengine
