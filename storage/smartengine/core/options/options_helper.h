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
#include "options/cf_options.h"
#include "options/db_options.h"
#include "table/table.h"

namespace smartengine {

namespace table {
class TableFactory;
}

namespace common {

DBOptions BuildDBOptions(const ImmutableDBOptions& immutable_db_options,
                         const MutableDBOptions& mutable_db_options);

ColumnFamilyOptions BuildColumnFamilyOptions(
    const ColumnFamilyOptions& ioptions,
    const MutableCFOptions& mutable_cf_options);

Status GetMutableOptionsFromStrings(
    const MutableCFOptions& base_options,
    const std::unordered_map<std::string, std::string>& options_map,
    MutableCFOptions* new_options);

Status GetMutableDBOptionsFromStrings(
    const MutableDBOptions& base_options,
    const std::unordered_map<std::string, std::string>& options_map,
    MutableDBOptions* new_options);


enum class OptionType {
  kBoolean,
  kInt,
  kVectorInt,
  kUInt,
  kUInt32T,
  kUInt64T,
  kSizeT,
  kString,
  kDouble,
  kCompactionStyle,
  kCompactionPri,
  kCompressionType,
  kVectorCompressionType,
  kTableFactory,
  kComparator,
  kCompactionFilter,
  kCompactionFilterFactory,
  kMemTableRepFactory,
  kBlockBasedTableIndexType,
  kFilterPolicy,
  kFlushBlockPolicyFactory,
  kChecksumType,
  kEncodingType,
  kWALRecoveryMode,
  kAccessHint,
  kInfoLogLevel,
  kUnknown
};

enum class OptionVerificationType {
  kNormal,
  kByName,           // The option is pointer typed so we can only verify
                     // based on it's name.
  kByNameAllowNull,  // Same as kByName, but it also allows the case
                     // where one of them is a nullptr.
  kDeprecated        // The option is no longer used in smartengine. The
                     // OptionsParser will still accept this option if it
                     // happen to exists in some Options file.  However, the
                     // parser will not include it in serialization and
                     // verification processes.
};

// A struct for storing constant option information such as option name,
// option type, and offset.
struct OptionTypeInfo {
  int offset;
  OptionType type;
  OptionVerificationType verification;
  bool is_mutable;
  int mutable_offset;
};

// A helper function to parse ColumnFamilyOptions::vector<CompressionType> from string
bool GetVectorCompressionType(const std::string& value,
                              std::vector<CompressionType>* out);
// A helper function to parse CompressionOptions from string
bool GetCompressionOptions(const std::string& value, CompressionOptions* out);

extern CompressionType get_compression_type(const ImmutableCFOptions &ioptions, const int level);

static std::unordered_map<std::string, OptionTypeInfo> db_options_type_info = {
    /*
     // not yet supported
      Env* env;
      std::shared_ptr<Cache> row_cache;
      std::shared_ptr<DeleteScheduler> delete_scheduler;
      std::shared_ptr<Logger> info_log;
      std::shared_ptr<RateLimiter> rate_limiter;
      std::shared_ptr<Statistics> statistics;
      std::vector<DbPath> db_paths;
      std::vector<std::shared_ptr<EventListener>> listeners;
     */
    {"use_direct_reads",
     {offsetof(struct DBOptions, use_direct_reads), OptionType::kBoolean,
      OptionVerificationType::kNormal, false, 0}},
    {"use_direct_writes",
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, false, 0}},
    {"allow_2pc",
     {offsetof(struct DBOptions, allow_2pc), OptionType::kBoolean,
      OptionVerificationType::kNormal, false, 0}},
    {"allow_os_buffer",
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, true, 0}},
    {"disableDataSync",
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, false, 0}},
    {"disable_data_sync",  // for compatibility
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, false, 0}},
    {"enable_thread_tracking",
     {offsetof(struct DBOptions, enable_thread_tracking), OptionType::kBoolean,
      OptionVerificationType::kNormal, false, 0}},
    {"writable_file_max_buffer_size",
     {offsetof(struct DBOptions, writable_file_max_buffer_size),
      OptionType::kSizeT, OptionVerificationType::kNormal, false, 0}},
    {"max_background_compactions",
     {offsetof(struct DBOptions, max_background_compactions), OptionType::kInt,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, max_background_compactions)}},
    {"base_background_compactions",
     {offsetof(struct DBOptions, base_background_compactions), OptionType::kInt,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, base_background_compactions)}},
    {"max_background_dumps",
     {offsetof(struct DBOptions, max_background_dumps), OptionType::kInt,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, max_background_dumps)}},
    {"dump_memtable_limit_size",
     {offsetof(struct DBOptions, dump_memtable_limit_size), OptionType::kSizeT,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, dump_memtable_limit_size)}},
    {"max_background_flushes",
     {offsetof(struct DBOptions, max_background_flushes), OptionType::kInt,
      OptionVerificationType::kNormal, false, 0}},
    {"table_cache_numshardbits",
     {offsetof(struct DBOptions, table_cache_numshardbits), OptionType::kInt,
      OptionVerificationType::kNormal, false, 0}},
    {"db_write_buffer_size",
     {offsetof(struct DBOptions, db_write_buffer_size), OptionType::kSizeT,
      OptionVerificationType::kNormal, false, 0}},
    {"db_total_write_buffer_size",
     {offsetof(struct DBOptions, db_total_write_buffer_size),
      OptionType::kSizeT, OptionVerificationType::kNormal, false, 0}},
    {"wal_dir",
     {offsetof(struct DBOptions, wal_dir), OptionType::kString,
      OptionVerificationType::kNormal, false, 0}},
    {"bytes_per_sync",
     {offsetof(struct DBOptions, bytes_per_sync), OptionType::kUInt64T,
      OptionVerificationType::kNormal, false, 0}},
    {"delete_obsolete_files_period_micros",
     {offsetof(struct DBOptions, delete_obsolete_files_period_micros),
      OptionType::kUInt64T, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, delete_obsolete_files_period_micros)}},
    {"max_total_wal_size",
     {offsetof(struct DBOptions, max_total_wal_size), OptionType::kUInt64T,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, max_total_wal_size)}},
    {"wal_bytes_per_sync",
     {offsetof(struct DBOptions, wal_bytes_per_sync), OptionType::kUInt64T,
      OptionVerificationType::kNormal, false, 0}},
    {"stats_dump_period_sec",
     {offsetof(struct DBOptions, stats_dump_period_sec), OptionType::kUInt,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, stats_dump_period_sec)}},
    {"allow_concurrent_memtable_write",
     {offsetof(struct DBOptions, allow_concurrent_memtable_write),
      OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
    {"wal_recovery_mode",
     {offsetof(struct DBOptions, wal_recovery_mode),
      OptionType::kWALRecoveryMode, OptionVerificationType::kNormal, false, 0}},
    {"enable_aio_wal_reader",
     {offsetof(struct DBOptions, enable_aio_wal_reader), OptionType::kBoolean,
      OptionVerificationType::kNormal, false, 0}},
    {"parallel_wal_recovery",
     {offsetof(struct DBOptions, parallel_wal_recovery), OptionType::kBoolean,
      OptionVerificationType::kNormal, false, 0}},
    {"parallel_recovery_thread_num",
     {offsetof(struct DBOptions, parallel_recovery_thread_num),
      OptionType::kUInt32T, OptionVerificationType::kNormal, false, 0}},
    {"mutex_backtrace_threshold_ns",
     {offsetof(struct DBOptions, mutex_backtrace_threshold_ns),
      OptionType::kUInt64T, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, mutex_backtrace_threshold_ns)}},
    {"avoid_flush_during_recovery",
     {offsetof(struct DBOptions, avoid_flush_during_recovery),
      OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
    {"avoid_flush_during_shutdown",
     {offsetof(struct DBOptions, avoid_flush_during_shutdown),
      OptionType::kBoolean, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, avoid_flush_during_shutdown)}},
    {"batch_group_slot_array_size",
     {offsetof(struct DBOptions, batch_group_slot_array_size),
      OptionType::kUInt64T, OptionVerificationType::kNormal, false, 0}},
    {"batch_group_max_group_size",
     {offsetof(struct DBOptions, batch_group_max_group_size),
      OptionType::kUInt64T, OptionVerificationType::kNormal, false, 0}},
    {"batch_group_max_leader_wait_time_us",
     {offsetof(struct DBOptions, batch_group_max_leader_wait_time_us),
      OptionType::kUInt64T, OptionVerificationType::kNormal, false, 0}},
    {"concurrent_writable_file_buffer_num",
     {offsetof(struct DBOptions, concurrent_writable_file_buffer_num),
      OptionType::kUInt64T, OptionVerificationType::kNormal, false, 0}},
    {"concurrent_writable_file_single_buffer_size",
     {offsetof(struct DBOptions, concurrent_writable_file_single_buffer_size),
      OptionType::kUInt64T, OptionVerificationType::kNormal, false, 0}},
    {"concurrent_writable_file_buffer_switch_limit",
     {offsetof(struct DBOptions, concurrent_writable_file_buffer_switch_limit),
      OptionType::kUInt64T, OptionVerificationType::kNormal, false, 0}},
    {"use_direct_write_for_wal",
     {offsetof(struct DBOptions, use_direct_write_for_wal),
      OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
    {"auto_shrink_enabled",
     {offsetof(struct DBOptions, auto_shrink_enabled), OptionType::kBoolean,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, auto_shrink_enabled)}},
    {"max_free_extent_percent",
     {offsetof(struct DBOptions, max_free_extent_percent), OptionType::kUInt64T,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, max_free_extent_percent)}},
    {"shrink_allocate_interval",
     {offsetof(struct DBOptions, shrink_allocate_interval),
      OptionType::kUInt64T, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, shrink_allocate_interval)}},
    {"max_shrink_extent_count",
     {offsetof(struct DBOptions, max_shrink_extent_count), OptionType::kUInt64T,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, max_shrink_extent_count)}},
    {"total_max_shrink_extent_count",
     {offsetof(struct DBOptions, total_max_shrink_extent_count),
      OptionType::kUInt64T, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, total_max_shrink_extent_count)}},
    {"idle_tasks_schedule_time",
     {offsetof(struct DBOptions, idle_tasks_schedule_time),
      OptionType::kUInt64T, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, idle_tasks_schedule_time)}},
    {"table_cache_size",
     {offsetof(struct DBOptions, table_cache_size), OptionType::kUInt64T,
      OptionVerificationType::kNormal, false, 0}},
    {"auto_shrink_schedule_interval",
     {offsetof(struct DBOptions, auto_shrink_schedule_interval),
      OptionType::kUInt64T, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, auto_shrink_schedule_interval)}},
    {"estimate_cost_depth",
     {offsetof(struct DBOptions, estimate_cost_depth), OptionType::kUInt64T,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, estimate_cost_depth)}},
    {"monitor_interval_ms",
     {offsetof(struct DBOptions, monitor_interval_ms), OptionType::kUInt64T,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, monitor_interval_ms)}},
    {"master_thread_compaction_enabled",
     {offsetof(struct DBOptions, master_thread_compaction_enabled),
      OptionType::kBoolean, OptionVerificationType::kNormal, true,
      offsetof(struct MutableDBOptions, master_thread_compaction_enabled)}},
    {"persistent_cache_dir",
     {offsetof(struct DBOptions, persistent_cache_dir), OptionType::kString,
      OptionVerificationType::kNormal, false, 0}},
    {"persistent_cache_size",
     {offsetof(struct DBOptions, persistent_cache_size), OptionType::kUInt64T,
      OptionVerificationType::kNormal, false, 0}},
    {"persistent_cache_mode",
     {offsetof(struct DBOptions, persistent_cache_mode), OptionType::kUInt64T,
      OptionVerificationType::kNormal, false, 0}},
    {"parallel_flush_log",
     {offsetof(struct DBOptions, parallel_flush_log),
      OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
};

// offset_of is used to get the offset of a class data member
// ex: offset_of(&ColumnFamilyOptions::num_levels)
// This call will return the offset of num_levels in ColumnFamilyOptions class
//
// This is the same as offsetof() but allow us to work with non standard-layout
// classes and structures
// refs:
// http://en.cppreference.com/w/cpp/concept/StandardLayoutType
// https://gist.github.com/graphitemaster/494f21190bb2c63c5516
template <typename T1, typename T2>
inline int offset_of(T1 T2::*member) {
  static T2 obj;
  return int(size_t(&(obj.*member)) - size_t(&obj));
}

static std::unordered_map<std::string, OptionTypeInfo> cf_options_type_info = {
    {"compaction_measure_io_stats",
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, false, 0}},
    {"disable_auto_compactions",
     {offset_of(&ColumnFamilyOptions::disable_auto_compactions),
      OptionType::kBoolean, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, disable_auto_compactions)}},
    {"filter_deletes",
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, true, 0}},
    {"level_compaction_dynamic_level_bytes",
     {offset_of(&ColumnFamilyOptions::level_compaction_dynamic_level_bytes),
      OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
    {"verify_checksums_in_compaction",
     {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, true, 0}},
    {"expanded_compaction_factor",
     {0, OptionType::kInt, OptionVerificationType::kDeprecated, true, 0}},
    {"level0_file_num_compaction_trigger",
     {offset_of(&ColumnFamilyOptions::level0_file_num_compaction_trigger),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, level0_file_num_compaction_trigger)}},
    {"level0_layer_num_compaction_trigger",
     {offset_of(&ColumnFamilyOptions::level0_layer_num_compaction_trigger),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, level0_layer_num_compaction_trigger)}},
    {"level1_extents_major_compaction_trigger",
     {offset_of(&ColumnFamilyOptions::level1_extents_major_compaction_trigger),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, level1_extents_major_compaction_trigger)}},
    {"level2_usage_percent",
     {offset_of(&ColumnFamilyOptions::level2_usage_percent),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, level2_usage_percent)}},
    {"max_write_buffer_number_to_maintain",
     {offset_of(&ColumnFamilyOptions::max_write_buffer_number_to_maintain),
      OptionType::kInt, OptionVerificationType::kNormal, false, 0}},
    {"min_write_buffer_number_to_merge",
     {offset_of(&ColumnFamilyOptions::min_write_buffer_number_to_merge),
      OptionType::kInt, OptionVerificationType::kNormal, false, 0}},
    {"source_compaction_factor",
     {0, OptionType::kInt, OptionVerificationType::kDeprecated, true, 0}},
    {"memtable_prefix_bloom_huge_page_tlb_size",
     {0, OptionType::kSizeT, OptionVerificationType::kDeprecated, true, 0}},
    {"write_buffer_size",
     {offset_of(&ColumnFamilyOptions::write_buffer_size), OptionType::kSizeT,
      OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, write_buffer_size)}},
    {"flush_delete_percent",
     {offset_of(&ColumnFamilyOptions::flush_delete_percent),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, flush_delete_percent)}},
    {"compaction_delete_percent",
     {offset_of(&ColumnFamilyOptions::compaction_delete_percent),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, compaction_delete_percent)}},
    {"flush_delete_percent_trigger",
     {offset_of(&ColumnFamilyOptions::flush_delete_percent_trigger),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, flush_delete_percent_trigger)}},
    {"flush_delete_record_trigger",
     {offset_of(&ColumnFamilyOptions::flush_delete_record_trigger),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, flush_delete_record_trigger)}},
    {"compression_per_level",
     {offset_of(&ColumnFamilyOptions::compression_per_level),
      OptionType::kVectorCompressionType, OptionVerificationType::kNormal,
      false, 0}},
    {"comparator",
     {offset_of(&ColumnFamilyOptions::comparator), OptionType::kComparator,
      OptionVerificationType::kByName, false, 0}},
    {"memtable_factory",
     {offset_of(&ColumnFamilyOptions::memtable_factory),
      OptionType::kMemTableRepFactory, OptionVerificationType::kByName, false,
      0}},
    {"table_factory",
     {offset_of(&ColumnFamilyOptions::table_factory), OptionType::kTableFactory,
      OptionVerificationType::kByName, false, 0}},
    {"scan_add_blocks_limit",
     {offset_of(&ColumnFamilyOptions::scan_add_blocks_limit),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, scan_add_blocks_limit)}},
    {"bottommost_level",
     {offset_of(&ColumnFamilyOptions::bottommost_level),
      OptionType::kInt, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, bottommost_level)}},
    {"compaction_task_extents_limit",
      {offset_of(&ColumnFamilyOptions::compaction_task_extents_limit),
       OptionType::kInt, OptionVerificationType::kNormal, true,
       offsetof(struct MutableCFOptions, compaction_task_extents_limit)}},
    {"background_disable_merge",
      {offset_of(&ColumnFamilyOptions::background_disable_merge),
      OptionType::kBoolean, OptionVerificationType::kNormal, true,
      offsetof(struct MutableCFOptions, background_disable_merge)}}
};

static std::unordered_map<std::string, OptionTypeInfo>
    block_based_table_type_info = {
        /* currently not supported
          std::shared_ptr<Cache> block_cache = nullptr;
          std::shared_ptr<Cache> block_cache_compressed = nullptr;
         */
        {"flush_block_policy_factory",
         {offsetof(table::BlockBasedTableOptions, flush_block_policy_factory),
          OptionType::kFlushBlockPolicyFactory, OptionVerificationType::kByName,
          false, 0}},
        {"cache_index_and_filter_blocks",
         {offsetof(table::BlockBasedTableOptions,
                   cache_index_and_filter_blocks),
          OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
        {"cache_index_and_filter_blocks_with_high_priority",
         {offsetof(table::BlockBasedTableOptions,
                   cache_index_and_filter_blocks_with_high_priority),
          OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
        {"pin_l0_filter_and_index_blocks_in_cache",
         {offsetof(table::BlockBasedTableOptions,
                   pin_l0_filter_and_index_blocks_in_cache),
          OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
        {"flush_fill_block_cache",
         {offsetof(table::BlockBasedTableOptions, flush_fill_block_cache),
          OptionType::kBoolean, OptionVerificationType::kNormal, true, 0}},
        {"checksum",
         {offsetof(table::BlockBasedTableOptions, checksum),
          OptionType::kChecksumType, OptionVerificationType::kNormal, false,
          0}},
        {"no_block_cache",
         {offsetof(table::BlockBasedTableOptions, no_block_cache),
          OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
        {"block_size",
         {offsetof(table::BlockBasedTableOptions, block_size),
          OptionType::kSizeT, OptionVerificationType::kNormal, false, 0}},
        {"block_size_deviation",
         {offsetof(table::BlockBasedTableOptions, block_size_deviation),
          OptionType::kInt, OptionVerificationType::kNormal, false, 0}},
        {"block_restart_interval",
         {offsetof(table::BlockBasedTableOptions, block_restart_interval),
          OptionType::kInt, OptionVerificationType::kNormal, false, 0}},
        {"index_block_restart_interval",
         {offsetof(table::BlockBasedTableOptions, index_block_restart_interval),
          OptionType::kInt, OptionVerificationType::kNormal, false, 0}},
        {"index_per_partition",
         {0, OptionType::kUInt64T, OptionVerificationType::kDeprecated, false,
          0}},
        {"skip_table_builder_flush",
         {0, OptionType::kBoolean, OptionVerificationType::kDeprecated, false,
          0}},
        {"format_version",
         {offsetof(table::BlockBasedTableOptions, format_version),
          OptionType::kUInt32T, OptionVerificationType::kNormal, false, 0}},
        {"verify_compression",
         {offsetof(table::BlockBasedTableOptions, verify_compression),
          OptionType::kBoolean, OptionVerificationType::kNormal, false, 0}},
        {"read_amp_bytes_per_bit",
         {offsetof(table::BlockBasedTableOptions, read_amp_bytes_per_bit),
          OptionType::kSizeT, OptionVerificationType::kNormal, false, 0}}};

static std::unordered_map<std::string, CompressionType>
    compression_type_string_map = {
        {"kNoCompression", kNoCompression},
        {"kSnappyCompression", kSnappyCompression},
        {"kZlibCompression", kZlibCompression},
        {"kBZip2Compression", kBZip2Compression},
        {"kLZ4Compression", kLZ4Compression},
        {"kLZ4HCCompression", kLZ4HCCompression},
        {"kXpressCompression", kXpressCompression},
        {"kZSTD", kZSTD},
        {"kZSTDNotFinalCompression", kZSTDNotFinalCompression},
        {"kDisableCompressionOption", kDisableCompressionOption}};

static std::unordered_map<std::string, table::ChecksumType>
    checksum_type_string_map = {{"kNoChecksum", table::kNoChecksum},
                                {"kCRC32c", table::kCRC32c},
                                {"kxxHash", table::kxxHash}};

static std::unordered_map<std::string, WALRecoveryMode>
    wal_recovery_mode_string_map = {
        {"kTolerateCorruptedTailRecords",
         WALRecoveryMode::kTolerateCorruptedTailRecords},
        {"kAbsoluteConsistency", WALRecoveryMode::kAbsoluteConsistency},
        {"kPointInTimeRecovery", WALRecoveryMode::kPointInTimeRecovery},
        {"kSkipAnyCorruptedRecords",
         WALRecoveryMode::kSkipAnyCorruptedRecords}};

}  // namespace common
}  // namespace smartengine
