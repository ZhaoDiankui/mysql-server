/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
#include "options/options_helper.h"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <vector>
#include "options/options.h"
#include "table/extent_table_factory.h"
#include "util/rate_limiter.h"
#include "util/string_util.h"

namespace smartengine {
using namespace table;
using namespace util;
using namespace db;
using namespace storage;
using namespace memtable;
using namespace cache;

namespace common {

DBOptions BuildDBOptions(const ImmutableDBOptions& immutable_db_options,
                         const MutableDBOptions& mutable_db_options) {
  DBOptions options;

  options.env = immutable_db_options.env;
  options.rate_limiter = immutable_db_options.rate_limiter;
  options.max_total_wal_size = mutable_db_options.max_total_wal_size;
  options.statistics = immutable_db_options.statistics;
  options.db_paths = immutable_db_options.db_paths;
  options.wal_dir = immutable_db_options.wal_dir;
  options.delete_obsolete_files_period_micros =
      mutable_db_options.delete_obsolete_files_period_micros;
  options.base_background_compactions =
      mutable_db_options.base_background_compactions;
  options.max_background_compactions =
      mutable_db_options.max_background_compactions;
  options.max_background_flushes = immutable_db_options.max_background_flushes;
  options.max_background_dumps = mutable_db_options.max_background_dumps;
  options.dump_memtable_limit_size = mutable_db_options.dump_memtable_limit_size;
  options.table_cache_numshardbits =
      immutable_db_options.table_cache_numshardbits;
  options.use_direct_reads = immutable_db_options.use_direct_reads;
  options.db_write_buffer_size = immutable_db_options.db_write_buffer_size;
  options.db_total_write_buffer_size = immutable_db_options.db_total_write_buffer_size;
  options.write_buffer_manager = immutable_db_options.write_buffer_manager;
  options.writable_file_max_buffer_size =
      immutable_db_options.writable_file_max_buffer_size;
  options.bytes_per_sync = immutable_db_options.bytes_per_sync;
  options.wal_bytes_per_sync = immutable_db_options.wal_bytes_per_sync;
  options.enable_thread_tracking = immutable_db_options.enable_thread_tracking;
  options.allow_concurrent_memtable_write =
      immutable_db_options.allow_concurrent_memtable_write;
  options.wal_recovery_mode = immutable_db_options.wal_recovery_mode;
  options.enable_aio_wal_reader = immutable_db_options.enable_aio_wal_reader;
  options.parallel_wal_recovery = immutable_db_options.parallel_wal_recovery;
  options.parallel_recovery_thread_num = immutable_db_options.parallel_recovery_thread_num;
  options.allow_2pc = immutable_db_options.allow_2pc;
  options.row_cache = immutable_db_options.row_cache;
  options.avoid_flush_during_recovery =
    immutable_db_options.avoid_flush_during_recovery;

  options.avoid_flush_during_shutdown =
      mutable_db_options.avoid_flush_during_shutdown;
  options.stats_dump_period_sec = mutable_db_options.stats_dump_period_sec;
  options.batch_group_slot_array_size =
      mutable_db_options.batch_group_slot_array_size;
  options.batch_group_max_group_size =
      mutable_db_options.batch_group_max_group_size;
  options.batch_group_max_leader_wait_time_us =
      mutable_db_options.batch_group_max_leader_wait_time_us;
  options.concurrent_writable_file_buffer_num =
      mutable_db_options.concurrent_writable_file_buffer_num;
  options.concurrent_writable_file_single_buffer_size =
      mutable_db_options.concurrent_writable_file_single_buffer_size;
  options.concurrent_writable_file_buffer_switch_limit =
      mutable_db_options.concurrent_writable_file_buffer_switch_limit;
  options.use_direct_write_for_wal =
      mutable_db_options.use_direct_write_for_wal;
  options.auto_shrink_enabled = mutable_db_options.auto_shrink_enabled;
  options.max_free_extent_percent = mutable_db_options.max_free_extent_percent;
  options.shrink_allocate_interval = mutable_db_options.shrink_allocate_interval;
  options.total_max_shrink_extent_count = mutable_db_options.total_max_shrink_extent_count;
  options.auto_shrink_schedule_interval = mutable_db_options.auto_shrink_schedule_interval;
  options.max_shrink_extent_count = mutable_db_options.max_shrink_extent_count;
  options.estimate_cost_depth = mutable_db_options.estimate_cost_depth;

  return options;
}

ColumnFamilyOptions BuildColumnFamilyOptions(
    const ColumnFamilyOptions& options,
    const MutableCFOptions& mutable_cf_options) {
  ColumnFamilyOptions cf_opts(options);

  // Memtable related options
  cf_opts.write_buffer_size = mutable_cf_options.write_buffer_size;
  cf_opts.flush_delete_percent = mutable_cf_options.flush_delete_percent;
  cf_opts.compaction_delete_percent = mutable_cf_options.compaction_delete_percent;
  cf_opts.flush_delete_percent_trigger = mutable_cf_options.flush_delete_percent_trigger;
  cf_opts.flush_delete_record_trigger = mutable_cf_options.flush_delete_record_trigger;

  // Compaction related options
  cf_opts.disable_auto_compactions =
      mutable_cf_options.disable_auto_compactions;
  cf_opts.level0_file_num_compaction_trigger =
      mutable_cf_options.level0_file_num_compaction_trigger;
  cf_opts.level0_layer_num_compaction_trigger =
      mutable_cf_options.level0_layer_num_compaction_trigger;
  cf_opts.level1_extents_major_compaction_trigger =
      mutable_cf_options.level1_extents_major_compaction_trigger;
  cf_opts.level2_usage_percent =
      mutable_cf_options.level2_usage_percent;

  // Misc options
  cf_opts.table_factory = options.table_factory;
  cf_opts.scan_add_blocks_limit = options.scan_add_blocks_limit;
  cf_opts.bottommost_level = options.bottommost_level;
  cf_opts.compaction_task_extents_limit = options.compaction_task_extents_limit;
  return cf_opts;
}

namespace {
template <typename T>
bool ParseEnum(const std::unordered_map<std::string, T>& type_map,
               const std::string& type, T* value) {
  auto iter = type_map.find(type);
  if (iter != type_map.end()) {
    *value = iter->second;
    return true;
  }
  return false;
}

template <typename T>
bool SerializeEnum(const std::unordered_map<std::string, T>& type_map,
                   const T& type, std::string* value) {
  for (const auto& pair : type_map) {
    if (pair.second == type) {
      *value = pair.first;
      return true;
    }
  }
  return false;
}

bool ParseVectorCompressionType(
    const std::string& value,
    std::vector<CompressionType>* compression_per_level) {
  compression_per_level->clear();
  size_t start = 0;
  while (start < value.size()) {
    size_t end = value.find(':', start);
    bool is_ok;
    CompressionType type;
    if (end == std::string::npos) {
      is_ok = ParseEnum<CompressionType>(compression_type_string_map,
                                         value.substr(start), &type);
      if (!is_ok) {
        return false;
      }
      compression_per_level->emplace_back(type);
      break;
    } else {
      is_ok = ParseEnum<CompressionType>(
          compression_type_string_map, value.substr(start, end - start), &type);
      if (!is_ok) {
        return false;
      }
      compression_per_level->emplace_back(type);
      start = end + 1;
    }
  }
  return true;
}

bool ParseCompressionOptions(
    const std::string& value, CompressionOptions* compression_options) {
  size_t start = 0;
  size_t end = value.find(':');
  if (end == std::string::npos) {
    return false;
  }
  compression_options->window_bits = ParseInt(value.substr(start, end - start));
  start = end + 1;
  end = value.find(':', start);
  if (end == std::string::npos) {
    return false;
  }
  compression_options->level = ParseInt(value.substr(start, end - start));
  start = end + 1;
  if (start >= value.size()) {
    return false;
  }
  end = value.find(':', start);
  compression_options->strategy = ParseInt(value.substr(start, value.size() - start));
  // max_dict_bytes is optional for backwards compatibility
  if (end != std::string::npos) {
    start = end + 1;
    if (start >= value.size()) {
      return false;
    }
    compression_options->max_dict_bytes = ParseInt(value.substr(start, value.size() - start));
  }
  return true;
}

bool ParseOptionHelper(char* opt_address, const OptionType& opt_type,
                       const std::string& value) {
  switch (opt_type) {
    case OptionType::kBoolean:
      *reinterpret_cast<bool*>(opt_address) = ParseBoolean("", value);
      break;
    case OptionType::kInt:
      *reinterpret_cast<int*>(opt_address) = ParseInt(value);
      break;
    case OptionType::kVectorInt:
      *reinterpret_cast<std::vector<int>*>(opt_address) = ParseVectorInt(value);
      break;
    case OptionType::kUInt:
      *reinterpret_cast<unsigned int*>(opt_address) = ParseUint32(value);
      break;
    case OptionType::kUInt32T:
      *reinterpret_cast<uint32_t*>(opt_address) = ParseUint32(value);
      break;
    case OptionType::kUInt64T:
      *reinterpret_cast<uint64_t*>(opt_address) = ParseUint64(value);
      break;
    case OptionType::kSizeT:
      *reinterpret_cast<size_t*>(opt_address) = ParseSizeT(value);
      break;
    case OptionType::kString:
      *reinterpret_cast<std::string*>(opt_address) = value;
      break;
    case OptionType::kDouble:
      *reinterpret_cast<double*>(opt_address) = ParseDouble(value);
      break;
    case OptionType::kCompressionType:
      return ParseEnum<CompressionType>(
          compression_type_string_map, value,
          reinterpret_cast<CompressionType*>(opt_address));
    case OptionType::kVectorCompressionType:
      return ParseVectorCompressionType(
          value, reinterpret_cast<std::vector<CompressionType>*>(opt_address));
    case OptionType::kChecksumType:
      return ParseEnum<ChecksumType>(
          checksum_type_string_map, value,
          reinterpret_cast<ChecksumType*>(opt_address));
    case OptionType::kWALRecoveryMode:
      return ParseEnum<WALRecoveryMode>(
          wal_recovery_mode_string_map, value,
          reinterpret_cast<WALRecoveryMode*>(opt_address));
    default:
      return false;
  }
  return true;
}

}  // anonymouse namespace

CompressionType get_compression_type(const ImmutableCFOptions &ioptions, const int level)
{
  CompressionType compression_type = kNoCompression;
  // If the user has specified a different compression level for each level,
  // then pick the compression for that level.
  if (!ioptions.compression_per_level.empty()) {
    assert(level >= 0 && level <= 2);
    const int n = static_cast<int>(ioptions.compression_per_level.size()) - 1;
    compression_type = ioptions.compression_per_level[std::max(0, std::min(level, n))];
  }

  return compression_type;
}

bool GetVectorCompressionType(const std::string& value,
                              std::vector<CompressionType>* out) {
  if (nullptr == out) {
    return false;
  }
  return ParseVectorCompressionType(value, out);
}

bool GetCompressionOptions(const std::string& value, CompressionOptions* out) {
  if (nullptr == out) {
    return false;
  }
  return ParseCompressionOptions(value, out);
}

Status GetMutableOptionsFromStrings(
    const MutableCFOptions& base_options,
    const std::unordered_map<std::string, std::string>& options_map,
    MutableCFOptions* new_options) {
  assert(new_options);
  *new_options = base_options;
  for (const auto& o : options_map) {
    try {
      auto iter = cf_options_type_info.find(o.first);
      if (iter == cf_options_type_info.end()) {
        return Status::InvalidArgument("Unrecognized option: " + o.first);
      }
      const auto& opt_info = iter->second;
      if (!opt_info.is_mutable) {
        return Status::InvalidArgument("Option not changeable: " + o.first);
      }
      bool is_ok = ParseOptionHelper(
          reinterpret_cast<char*>(new_options) + opt_info.mutable_offset,
          opt_info.type, o.second);
      if (!is_ok) {
        return Status::InvalidArgument("Error parsing " + o.first);
      }
    } catch (std::exception& e) {
      return Status::InvalidArgument("Error parsing " + o.first + ":" +
                                     std::string(e.what()));
    }
  }
  return Status::OK();
}

Status GetMutableDBOptionsFromStrings(
    const MutableDBOptions& base_options,
    const std::unordered_map<std::string, std::string>& options_map,
    MutableDBOptions* new_options) {
  assert(new_options);
  *new_options = base_options;
  for (const auto& o : options_map) {
    try {
      auto iter = db_options_type_info.find(o.first);
      if (iter == db_options_type_info.end()) {
        return Status::InvalidArgument("Unrecognized option: " + o.first);
      }
      const auto& opt_info = iter->second;
      if (!opt_info.is_mutable) {
        return Status::InvalidArgument("Option not changeable: " + o.first);
      }
      bool is_ok = ParseOptionHelper(
          reinterpret_cast<char*>(new_options) + opt_info.mutable_offset,
          opt_info.type, o.second);
      if (!is_ok) {
        return Status::InvalidArgument("Error parsing " + o.first);
      }
    } catch (std::exception& e) {
      return Status::InvalidArgument("Error parsing " + o.first + ":" +
                                     std::string(e.what()));
    }
  }
  return Status::OK();
}

Status ParseDBOption(const std::string& name, const std::string& org_value,
                     DBOptions* new_options,
                     bool input_strings_escaped = false) {
  const std::string& value =
      input_strings_escaped ? UnescapeOptionString(org_value) : org_value;
  try {
    if (name == "rate_limiter_bytes_per_sec") {
      new_options->rate_limiter.reset(
          NewGenericRateLimiter(static_cast<int64_t>(ParseUint64(value))));
    } else {
      auto iter = db_options_type_info.find(name);
      if (iter == db_options_type_info.end()) {
        return Status::InvalidArgument("Unrecognized option DBOptions:", name);
      }
      const auto& opt_info = iter->second;
      if (opt_info.verification != OptionVerificationType::kDeprecated &&
          ParseOptionHelper(
              reinterpret_cast<char*>(new_options) + opt_info.offset,
              opt_info.type, value)) {
        return Status::OK();
      }
      switch (opt_info.verification) {
        case OptionVerificationType::kByName:
        case OptionVerificationType::kByNameAllowNull:
          return Status::NotSupported("Deserializing the specified DB option " +
                                      name + " is not supported");
        case OptionVerificationType::kDeprecated:
          return Status::OK();
        default:
          return Status::InvalidArgument(
              "Unable to parse the specified DB option " + name);
      }
    }
  } catch (const std::exception&) {
    return Status::InvalidArgument("Unable to parse DBOptions:", name);
  }
  return Status::OK();
}

}  // namespace common
}  // namespace smartengine
