/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012, Monty Program Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef SMARTENGINE_STATUS_VARS_H_
#define SMARTENGINE_STATUS_VARS_H_

#include "util/se_counter.h"
#include "my_inttypes.h"
#include "core/util/to_string.h"

struct SHOW_VAR;

namespace smartengine
{

#ifdef WITH_XENGINE_COMPATIBLE_MODE
#define SE_STATUS_VAR_PREFIX "Xengine"
#else
#define SE_STATUS_VAR_PREFIX "Smartengine"
#endif

#if defined(HAVE_SCHED_GETCPU)
#define SE_INDEXER get_sched_indexer_t
#else
#define SE_INDEXER thread_id_indexer_t
#endif

enum operation_type
{
  ROWS_DELETED = 0,
  ROWS_INSERTED,
  ROWS_READ,
  ROWS_UPDATED,
  ROWS_MAX
};

/** Global statistics struct used inside SE */
struct SeGlobalStats
{
  ib_counter_t<ulonglong, 64, SE_INDEXER> rows_[ROWS_MAX];

  /**system_rows_ stats are only for system tables. They are not counted in rows_* stats.*/
  ib_counter_t<ulonglong, 64, SE_INDEXER> system_rows_[ROWS_MAX];

  std::atomic<uint64_t> snapshot_conflict_errors_;
  std::atomic<uint64_t> wal_group_syncs_;
  std::atomic<uint64_t> max_level0_layers_;
  std::atomic<uint64_t> max_imm_numbers_;
  std::atomic<uint64_t> max_level0_fragmentation_rate_;
  std::atomic<uint64_t> max_level1_fragmentation_rate_;
  std::atomic<uint64_t> max_level2_fragmentation_rate_;
  std::atomic<uint64_t> max_level0_delete_percent_;
  std::atomic<uint64_t> max_level1_delete_percent_;
  std::atomic<uint64_t> max_level2_delete_percent_;
  std::atomic<uint64_t> all_flush_megabytes_;
  std::atomic<uint64_t> all_compaction_megabytes_;
  std::atomic<uint64_t> top1_subtable_size_;
  std::atomic<uint64_t> top2_subtable_size_;
  std::atomic<uint64_t> top3_subtable_size_;
  std::atomic<uint64_t> top1_mod_mem_info_;
  std::atomic<uint64_t> top2_mod_mem_info_;
  std::atomic<uint64_t> top3_mod_mem_info_;
  std::atomic<uint64_t> global_external_fragmentation_rate_;

  SeGlobalStats();
};

struct SeStatusVars
{
  /**Query perf context counter variables*/
  uint64_t block_cache_miss_;
  uint64_t block_cache_hit_;
  uint64_t block_cache_add_;
  uint64_t block_cache_index_miss_;
  uint64_t block_cache_index_hit_;
  uint64_t block_cache_filter_miss_;
  uint64_t block_cache_filter_hit_;
  uint64_t block_cache_data_miss_;
  uint64_t block_cache_data_hit_;
  uint64_t row_cache_add_;
  uint64_t row_cache_hit_;
  uint64_t row_cache_miss_;
  uint64_t memtable_hit_;
  uint64_t memtable_miss_;
  uint64_t number_keys_written_;
  uint64_t number_keys_read_;
  uint64_t number_keys_updated_;
  uint64_t bytes_written_;
  uint64_t bytes_read_;
  uint64_t block_cachecompressed_miss_;
  uint64_t block_cachecompressed_hit_;
  uint64_t wal_synced_;
  uint64_t wal_bytes_;
  uint64_t write_self_;
  uint64_t write_other_;
  uint64_t write_wal_;
  uint64_t number_superversion_acquires_;
  uint64_t number_superversion_releases_;
  uint64_t number_superversion_cleanups_;
  uint64_t number_block_not_compressed_;
  /**Global statistic variables*/
  uint64_t snapshot_conflict_errors_;
  uint64_t wal_group_syncs_;
  uint64_t rows_deleted_;
  uint64_t rows_inserted_;
  uint64_t rows_updated_;
  uint64_t rows_read_;
  uint64_t system_rows_deleted_;
  uint64_t system_rows_inserted_;
  uint64_t system_rows_updated_;
  uint64_t system_rows_read_;
  uint64_t max_level0_layers_;
  uint64_t max_imm_numbers_;
  uint64_t max_level0_fragmentation_rate_;
  uint64_t max_level1_fragmentation_rate_;
  uint64_t max_level2_fragmentation_rate_;
  uint64_t max_level0_delete_percent_;
  uint64_t max_level1_delete_percent_;
  uint64_t max_level2_delete_percent_;
  uint64_t all_flush_megabytes_;
  uint64_t all_compaction_megabytes_;
  uint64_t top1_subtable_size_;
  uint64_t top2_subtable_size_;
  uint64_t top3_subtable_size_;
  uint64_t top1_mod_mem_info_;
  uint64_t top2_mod_mem_info_;
  uint64_t top3_mod_mem_info_;
  uint64_t global_external_fragmentation_rate_;
  uint64_t write_transaction_count_;
  uint64_t pipeline_group_count_;
  uint64_t pipeline_group_wait_timeout_count_;
  uint64_t pipeline_copy_log_size_;
  uint64_t pipeline_copy_log_count_;
  uint64_t pipeline_flush_log_size_;
  uint64_t pipeline_flush_log_count_;
  uint64_t pipeline_flush_log_sync_count_;
  uint64_t pipeline_flush_log_not_sync_count_;

  SeStatusVars();
  void reset();
  DECLARE_TO_STRING()
};

extern SeGlobalStats global_stats;

extern SHOW_VAR *se_show_vars_export;

} // namespace smartengine

#endif // end of SMARTENGINE_STATUS_VARS_H_
