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
#include "options/options.h"

namespace smartengine {
namespace common {

struct ImmutableDBOptions {
  ImmutableDBOptions();
  explicit ImmutableDBOptions(const DBOptions& options);

  void Dump() const;

  util::Env* env;
  std::shared_ptr<util::RateLimiter> rate_limiter;
  std::shared_ptr<monitor::Statistics> statistics;
  std::vector<DbPath> db_paths;
  std::string wal_dir;
  int max_background_flushes;
  int table_cache_numshardbits;
  bool use_direct_reads;
  size_t db_write_buffer_size;
  size_t db_total_write_buffer_size;
  std::shared_ptr<db::WriteBufferManager> write_buffer_manager;
  //TODO(Zhao Dongsheng), useless?
  size_t writable_file_max_buffer_size;
  uint64_t bytes_per_sync;
  uint64_t wal_bytes_per_sync;
  bool enable_thread_tracking;
  bool allow_concurrent_memtable_write;
  WALRecoveryMode wal_recovery_mode;
  bool enable_aio_wal_reader;
  bool parallel_wal_recovery;
  uint32_t parallel_recovery_thread_num;
  bool allow_2pc;
  //TODO(Zhao Dongsheng), Putting the member variable "row cache" here is not appropriate.
  std::shared_ptr<cache::RowCache> row_cache;
  bool avoid_flush_during_recovery;
  uint64_t table_cache_size;
  std::string persistent_cache_dir;
  uint64_t persistent_cache_size;
  uint64_t persistent_cache_mode;
  bool parallel_flush_log;
};

struct MutableDBOptions
{
  MutableDBOptions();
  explicit MutableDBOptions(const MutableDBOptions& options) = default;
  explicit MutableDBOptions(const DBOptions& options);
  MutableDBOptions &operator=(const MutableDBOptions &options) = default;


  void Dump() const;

  int base_background_compactions;
  int max_background_compactions;
  bool avoid_flush_during_shutdown;
  uint64_t max_total_wal_size;
  uint64_t delete_obsolete_files_period_micros;
  unsigned int stats_dump_period_sec;
  uint64_t batch_group_slot_array_size;
  uint64_t batch_group_max_group_size;
  uint64_t batch_group_max_leader_wait_time_us;
  uint64_t concurrent_writable_file_buffer_num;
  uint64_t concurrent_writable_file_single_buffer_size;
  uint64_t concurrent_writable_file_buffer_switch_limit;
  bool use_direct_write_for_wal;
  uint64_t mutex_backtrace_threshold_ns;
  int max_background_dumps;
  uint64_t dump_memtable_limit_size; // close incremental checkpoint func when set 0
  bool auto_shrink_enabled;
  uint64_t max_free_extent_percent;
  uint64_t shrink_allocate_interval;
  uint64_t max_shrink_extent_count;
  uint64_t total_max_shrink_extent_count;
  uint64_t idle_tasks_schedule_time;
  uint64_t auto_shrink_schedule_interval;
  uint64_t estimate_cost_depth;
  uint64_t monitor_interval_ms;
  bool master_thread_compaction_enabled;
};

}  // namespace common
}  // namespace smartengine
