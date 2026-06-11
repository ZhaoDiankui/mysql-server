/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "options/cf_options.h"
#include "table/table.h"
#include "util/compression.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <cassert>
#include "options/db_options.h"

namespace smartengine {
using namespace db;
using namespace util;

namespace common {
ImmutableCFOptions::ImmutableCFOptions(const Options& options)
    : ImmutableCFOptions(ImmutableDBOptions(options), options)
{}

ImmutableCFOptions::ImmutableCFOptions(const ImmutableDBOptions& db_options,
                                       const ColumnFamilyOptions& cf_options)
    : user_comparator(cf_options.comparator),
      min_write_buffer_number_to_merge(cf_options.min_write_buffer_number_to_merge),
      max_write_buffer_number_to_maintain(cf_options.max_write_buffer_number_to_maintain),
      statistics(db_options.statistics.get()),
      rate_limiter(db_options.rate_limiter.get()),
      env(db_options.env),
      db_paths(db_options.db_paths),
      memtable_factory(cf_options.memtable_factory.get()),
      table_factory(cf_options.table_factory.get()),
      compression_per_level(cf_options.compression_per_level),
      compression_opts(cf_options.compression_opts),
      level_compaction_dynamic_level_bytes(cf_options.level_compaction_dynamic_level_bytes),
      row_cache(db_options.row_cache)
{}

void ImmutableCFOptions::dump() const
{
  if (nullptr == user_comparator) {
    __SE_LOG(INFO, "                          user_comparator: %p", user_comparator);
  } else {
    __SE_LOG(INFO, "                          user_comparator: %s", user_comparator->Name());
  }

  __SE_LOG(INFO, "         min_write_buffer_number_to_merge: %d", min_write_buffer_number_to_merge);
  __SE_LOG(INFO, "      max_write_buffer_number_to_maintain: %d", max_write_buffer_number_to_maintain);
  __SE_LOG(INFO, "                               statistics: %p", statistics);
  __SE_LOG(INFO, "                             rate_limiter: %p", rate_limiter);
  __SE_LOG(INFO, "                                      env: %p", env);

  for (uint32_t i = 0; i < db_paths.size(); ++i) {
    __SE_LOG(INFO, "                            Options.db_path[%d]: %s", i, db_paths[i].path.c_str());
  }

  if (nullptr == memtable_factory) {
    __SE_LOG(INFO, "                               memtable_factory: %p", memtable_factory);
  } else {
    __SE_LOG(INFO, "                               memtable_factory: %s", memtable_factory->Name());
  }

  if (nullptr == table_factory) {
    __SE_LOG(INFO, "                                  table_factory: %p", table_factory);
  } else {
    __SE_LOG(INFO, "                                  table_factory: %s", table_factory->Name());
  }

  for (uint32_t i = 0; i < compression_per_level.size(); ++i) {
    __SE_LOG(INFO, "                                compression[%d]: %s",
        i, CompressionTypeToString(compression_per_level[i]).c_str());
  }

  __SE_LOG(INFO, "               level_compaction_dynamic_level_bytes: %d", level_compaction_dynamic_level_bytes);
  __SE_LOG(INFO, "                                          row_cache: %p", row_cache.get());
}

void MutableCFOptions::Dump() const
{
  __SE_LOG(INFO, "                       write_buffer_size: %ld", write_buffer_size);
  __SE_LOG(INFO, "                     flush_delete_percent: %d", flush_delete_percent);
  __SE_LOG(INFO, "                compaction_delete_percent: %d", compaction_delete_percent);
  __SE_LOG(INFO, "             flush_delete_percent_trigger: %d", flush_delete_percent_trigger);
  __SE_LOG(INFO, "              flush_delete_record_trigger: %d", flush_delete_record_trigger);
  __SE_LOG(INFO, "                 disable_auto_compactions: %d", disable_auto_compactions);
  __SE_LOG(INFO, "       level0_file_num_compaction_trigger: %d", level0_file_num_compaction_trigger);
  __SE_LOG(INFO, "      level0_layer_num_compaction_trigger: %d", level0_layer_num_compaction_trigger);
  __SE_LOG(INFO, "  level1_extents_major_compaction_trigger: %d", level1_extents_major_compaction_trigger);
  __SE_LOG(INFO, "                    level2_usage_percent: %ld", level2_usage_percent);
  __SE_LOG(INFO, "                   scan_add_blocks_limit: %ld", scan_add_blocks_limit);
  __SE_LOG(INFO, "                         bottommost_level: %d", bottommost_level);
  __SE_LOG(INFO, "            compaction_task_extents_limit: %d", compaction_task_extents_limit);
}

} // namespace common
}  // namespace smartengine
