/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <memory>
#include <vector>
#include "memtable/memtablerep.h"

namespace smartengine {
namespace common {

class Slice;
struct Options;
// DB contents are stored in a set of blocks, each of which holds a
// sequence of key,value pairs.  Each block may be compressed before
// being stored in a file.  The following enum describes which
// compression method (if any) is used to compress a block.
enum CompressionType {
  // NOTE: do not change the values of existing entries, as these are
  // part of the persistent format on disk.
  kNoCompression = 0x0,
  kSnappyCompression = 0x1,
  kZlibCompression = 0x2,
  kBZip2Compression = 0x3,
  kLZ4Compression = 0x4,
  kLZ4HCCompression = 0x5,
  kXpressCompression = 0x6,
  kZSTD = 0x7,

  // Only use kZSTDNotFinalCompression if you have to use ZSTD lib older than
  // 0.8.0 or consider a possibility of downgrading the service or copying
  // the database files to another service running with an older version of
  // RocksDB that doesn't have kZSTD. Otherwise, you should use kZSTD. We will
  // eventually remove the option from the public API.
  kZSTDNotFinalCompression = 0x40,

  // kDisableCompressionOption is used to disable some compression options.
  kDisableCompressionOption = 0xff,
};

//TODO (Zhao Dongsheng): Clean up the enumeration values in 'CompressionType'.
inline bool is_valid_compression_type(CompressionType compress_type)
{
  return (kNoCompression == compress_type) ||
         (kLZ4Compression == compress_type) ||
         (kZlibCompression == compress_type) ||
         (kZSTD == compress_type);
}

// Compression options for different compression algorithms like Zlib
struct CompressionOptions {
  int window_bits;
  int level;
  int strategy;
  // Maximum size of dictionary used to prime the compression library. Currently
  // this dictionary will be constructed by sampling the first output file in a
  // subcompaction when the target level is bottommost. This dictionary will be
  // loaded into the compression library before compressing/uncompressing each
  // data block of subsequent files in the subcompaction. Effectively, this
  // improves compression ratios when there are repetitions across data blocks.
  // A value of 0 indicates the feature is disabled.
  // Default: 0.
  uint32_t max_dict_bytes;

  CompressionOptions()
      : window_bits(-14), level(-1), strategy(0), max_dict_bytes(0) {}
  CompressionOptions(int wbits, int _lev, int _strategy, int _max_dict_bytes)
      : window_bits(wbits),
        level(_lev),
        strategy(_strategy),
        max_dict_bytes(_max_dict_bytes) {}
};

struct AdvancedColumnFamilyOptions {
  // The minimum number of write buffers that will be merged together
  // before writing to storage.  If set to 1, then
  // all write buffers are flushed to L0 as individual files and this increases
  // read amplification because a get request has to check in all of these
  // files. Also, an in-memory merge may result in writing lesser
  // data to storage if there are duplicate records in each of these
  // individual write buffers.  Default: 1
  int min_write_buffer_number_to_merge = 1;

  // The total maximum number of write buffers to maintain in memory including
  // copies of buffers that have already been flushed.  Unlike
  // max_write_buffer_number, this parameter does not affect flushing.
  // This controls the minimum amount of write history that will be available
  // in memory for conflict checking when Transactions are used.
  //
  // When using an OptimisticTransactionDB:
  // If this value is too low, some transactions may fail at commit time due
  // to not being able to determine whether there were any write conflicts.
  //
  // When using a TransactionDB:
  // If Transaction::SetSnapshot is used, TransactionDB will read either
  // in-memory write buffers or SST files to do write-conflict checking.
  // Increasing this value can reduce the number of reads to SST files
  // done for conflict detection.
  //
  // Setting this value to 0 will cause write buffers to be freed immediately
  // after they are flushed.
  // If this value is set to -1, 'max_write_buffer_number' will be used.
  //
  // Default:
  // If using a TransactionDB/OptimisticTransactionDB, the default value will
  // be set to the value of 'max_write_buffer_number' if it is not explicitly
  // set by the user.  Otherwise, the default is 0.
  int max_write_buffer_number_to_maintain = 0;

  // Different levels can have different compression policies. There
  // are cases where most lower levels would like to use quick compression
  // algorithms while the higher levels (which have more data) use
  // compression algorithms that have better compression but could
  // be slower. This array, if non-empty, should have an entry for
  // each level of the database; these override the value specified in
  // the previous field 'compression'.
  //
  // NOTICE if level_compaction_dynamic_level_bytes=true,
  // compression_per_level[0] still determines L0, but other elements
  // of the array are based on base level (the level L0 files are merged
  // to), and may not match the level users see from info log for metadata.
  // If L0 files are merged to level-n, then, for i>0, compression_per_level[i]
  // determines compaction type for level n+i-1.
  // For example, if we have three 5 levels, and we determine to merge L0
  // data to L4 (which means L1..L3 will be empty), then the new files go to
  // L4 uses compression type compression_per_level[1].
  // If now L0 is merged to L2. Data goes to L2 will be compressed
  // according to compression_per_level[1], L3 using compression_per_level[2]
  // and L4 using compression_per_level[3]. Compaction for each level can
  // change when data grows.
  std::vector<CompressionType> compression_per_level;

  // used in build-index case, we need assure all records
  // maintain existed during flush&compaction until build-index process finished
  bool background_disable_merge = false;

  // If true, RocksDB will pick target size of each level dynamically.
  // We will pick a base level b >= 1. L0 will be directly merged into level b,
  // instead of always into level 1. Level 1 to b-1 need to be empty.
  // We try to pick b and its target size so that
  // 1. target size is in the range of
  //   (max_bytes_for_level_base / max_bytes_for_level_multiplier,
  //    max_bytes_for_level_base]
  // 2. target size of the last level (level num_levels-1) equals to extra size
  //    of the level.
  // At the same time max_bytes_for_level_multiplier and
  // max_bytes_for_level_multiplier_additional are still satisfied.
  //
  // With this option on, from an empty DB, we make last level the base level,
  // which means merging L0 data into the last level, until it exceeds
  // max_bytes_for_level_base. And then we make the second last level to be
  // base level, to start to merge L0 data to second last level, with its
  // target size to be 1/max_bytes_for_level_multiplier of the last level's
  // extra size. After the data accumulates more so that we need to move the
  // base level to the third last one, and so on.
  //
  // For example, assume max_bytes_for_level_multiplier=10, num_levels=6,
  // and max_bytes_for_level_base=10MB.
  // Target sizes of level 1 to 5 starts with:
  // [- - - - 10MB]
  // with base level is level. Target sizes of level 1 to 4 are not applicable
  // because they will not be used.
  // Until the size of Level 5 grows to more than 10MB, say 11MB, we make
  // base target to level 4 and now the targets looks like:
  // [- - - 1.1MB 11MB]
  // While data are accumulated, size targets are tuned based on actual data
  // of level 5. When level 5 has 50MB of data, the target is like:
  // [- - - 5MB 50MB]
  // Until level 5's actual size is more than 100MB, say 101MB. Now if we keep
  // level 4 to be the base level, its target size needs to be 10.1MB, which
  // doesn't satisfy the target size range. So now we make level 3 the target
  // size and the target sizes of the levels look like:
  // [- - 1.01MB 10.1MB 101MB]
  // In the same way, while level 5 further grows, all levels' targets grow,
  // like
  // [- - 5MB 50MB 500MB]
  // Until level 5 exceeds 1000MB and becomes 1001MB, we make level 2 the
  // base level and make levels' target sizes like this:
  // [- 1.001MB 10.01MB 100.1MB 1001MB]
  // and go on...
  //
  // By doing it, we give max_bytes_for_level_multiplier a priority against
  // max_bytes_for_level_base, for a more predictable LSM tree shape. It is
  // useful to limit worse case space amplification.
  //
  // max_bytes_for_level_multiplier_additional is ignored with this flag on.
  //
  // Turning this feature on or off for an existing DB can cause unexpected
  // LSM tree structure so it's not recommended.
  //
  // NOTE: this option is experimental
  //
  // Default: false
  bool level_compaction_dynamic_level_bytes = false;

  // This is a factory that provides MemTableRep objects.
  // Default: a factory that provides a skip-list-based implementation of
  // MemTableRep.
  std::shared_ptr<memtable::MemTableRepFactory> memtable_factory =
      std::shared_ptr<memtable::SkipListFactory>(new memtable::SkipListFactory);

  // Create ColumnFamilyOptions with default values for all fields
  AdvancedColumnFamilyOptions();
  // Create ColumnFamilyOptions from Options
  explicit AdvancedColumnFamilyOptions(const Options& options);
};

}  // namespace common
}  // namespace smartengine
