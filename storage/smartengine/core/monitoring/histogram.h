//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <atomic>
#include <cassert>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace smartengine {
namespace monitor {

/**
 * Keep adding histogram's here.
 * Any histogram whould have value less than HISTOGRAM_ENUM_MAX
 * Add a new Histogram by assigning it the current value of HISTOGRAM_ENUM_MAX
 * Add a string representation in HistogramsNameMap below
 * And increment HISTOGRAM_ENUM_MAX
 */
enum Histograms : uint32_t {
  DB_GET = 0,
  DB_WRITE,
  COMPACTION_TIME,
  SUBCOMPACTION_SETUP_TIME,
  TABLE_SYNC_MICROS,
  COMPACTION_OUTFILE_SYNC_MICROS,
  WAL_FILE_SYNC_MICROS,
  MANIFEST_FILE_SYNC_MICROS,
  // TIME SPENT IN IO DURING TABLE OPEN
  TABLE_OPEN_IO_MICROS,
  DB_MULTIGET,
  READ_BLOCK_COMPACTION_MICROS,
  READ_BLOCK_GET_MICROS,
  WRITE_RAW_BLOCK_MICROS,
  STALL_L0_SLOWDOWN_COUNT,
  STALL_MEMTABLE_COMPACTION_COUNT,
  STALL_L0_NUM_FILES_COUNT,
  HARD_RATE_LIMIT_DELAY_COUNT,
  SOFT_RATE_LIMIT_DELAY_COUNT,
  NUM_FILES_IN_SINGLE_COMPACTION,
  DB_SEEK,
  WRITE_STALL,
  SST_READ_MICROS,
  // The number of subcompactions actually scheduled during a compaction
  NUM_SUBCOMPACTIONS_SCHEDULED,
  // Value size distribution in each operation
  BYTES_PER_READ,
  BYTES_PER_WRITE,
  BYTES_PER_MULTIGET,

  // number of bytes compressed/decompressed
  // number of bytes is when uncompressed; i.e. before/after respectively
  BYTES_COMPRESSED,
  BYTES_DECOMPRESSED,
  COMPRESSION_TIMES_NANOS,
  DECOMPRESSION_TIMES_NANOS,
  ENTRY_PER_LOG_COPY,
  BYTES_PER_LOG_COPY,
  TIME_PER_LOG_COPY,
  BYTES_PER_LOG_WRITE,
  TIME_PER_LOG_WRITE,
  PIPLINE_GROUP_SIZE,
  PIPLINE_LOOP_COUNT,
  PIPLINE_TRY_LOG_COPY_COUNT,
  PIPLINE_TRY_LOG_WRITE_COUNT,
  PIPLINE_CONCURRENT_RUNNING_WORKER_THERADS,
  PIPLINE_LOG_QUEUE_LENGTH,
  PIPLINE_MEM_QUEUE_LENGTH,
  PIPLINE_COMMIT_QUEUE_LENGTH,
  DEMO_WATCH_TIME_NANOS,
  HISTOGRAM_ENUM_MAX,  // TODO(ldemailly): enforce HistogramsNameMap match
};

const std::vector<std::pair<Histograms, std::string>> HistogramsNameMap = {
    {DB_GET, "smartengine.db.get.micros"},
    {DB_WRITE, "smartengine.db.write.micros"},
    {COMPACTION_TIME, "smartengine.compaction.times.micros"},
    {SUBCOMPACTION_SETUP_TIME, "smartengine.compactionjob.setup.times.micros"},
    {TABLE_SYNC_MICROS, "smartengine.table.sync.micros"},
    {COMPACTION_OUTFILE_SYNC_MICROS, "smartengine.compaction.outfile.sync.micros"},
    {WAL_FILE_SYNC_MICROS, "smartengine.wal.file.sync.micros"},
    {MANIFEST_FILE_SYNC_MICROS, "smartengine.manifest.file.sync.micros"},
    {TABLE_OPEN_IO_MICROS, "smartengine.table.open.io.micros"},
    {DB_MULTIGET, "smartengine.db.multiget.micros"},
    {READ_BLOCK_COMPACTION_MICROS, "smartengine.read.block.compaction.micros"},
    {READ_BLOCK_GET_MICROS, "smartengine.read.block.get.micros"},
    {WRITE_RAW_BLOCK_MICROS, "smartengine.write.raw.block.micros"},
    {STALL_L0_SLOWDOWN_COUNT, "smartengine.l0.slowdown.count"},
    {STALL_MEMTABLE_COMPACTION_COUNT, "smartengine.memtable.compaction.count"},
    {STALL_L0_NUM_FILES_COUNT, "smartengine.num.files.stall.count"},
    {HARD_RATE_LIMIT_DELAY_COUNT, "smartengine.hard.rate.limit.delay.count"},
    {SOFT_RATE_LIMIT_DELAY_COUNT, "smartengine.soft.rate.limit.delay.count"},
    {NUM_FILES_IN_SINGLE_COMPACTION, "smartengine.numextents.in.singlecompaction"},
    {DB_SEEK, "smartengine.db.seek.micros"},
    {WRITE_STALL, "smartengine.db.write.stall"},
    {SST_READ_MICROS, "smartengine.sst.read.micros"},
    {NUM_SUBCOMPACTIONS_SCHEDULED, "smartengine.num.compactiontasks.scheduled"},
    {BYTES_PER_READ, "smartengine.bytes.per.read"},
    {BYTES_PER_WRITE, "smartengine.bytes.per.write"},
    {BYTES_PER_MULTIGET, "smartengine.bytes.per.multiget"},
    {BYTES_COMPRESSED, "smartengine.bytes.compressed"},
    {BYTES_DECOMPRESSED, "smartengine.bytes.decompressed"},
    {COMPRESSION_TIMES_NANOS, "smartengine.compression.times.nanos"},
    {DECOMPRESSION_TIMES_NANOS, "smartengine.decompression.times.nanos"},
    {ENTRY_PER_LOG_COPY, "smartengine.log.per.copy.num"},
    {BYTES_PER_LOG_COPY, "smartengine.log.per.copy.bytes"},
    {TIME_PER_LOG_COPY, "smartengine.log.per.copy.time.nanos"},
    {BYTES_PER_LOG_WRITE, "smartengine.log.per.write.bytes"},
    {TIME_PER_LOG_WRITE, "smartengine.log.per.write.time.nanos"},
    {PIPLINE_GROUP_SIZE, "smartengine.pipline.group.size"},
    {PIPLINE_LOOP_COUNT, "smartengine.pipline.loop.count"},
    {PIPLINE_TRY_LOG_COPY_COUNT, "smartengine.pipline.loop.try.log.copy.count"},
    {PIPLINE_TRY_LOG_WRITE_COUNT, "smartengine.pipline.loop.try.log.write.count"},
    {PIPLINE_CONCURRENT_RUNNING_WORKER_THERADS,
     "smartengine.pipline.concurrent.running.worker.threads"},
    {PIPLINE_LOG_QUEUE_LENGTH, "smartengine.pipline.log.quque.length"},
    {PIPLINE_MEM_QUEUE_LENGTH, "smartengine.pipline.mem.queue.length"},
    {PIPLINE_COMMIT_QUEUE_LENGTH, "smartengine.pipline.commit.queue.length"},
    {DEMO_WATCH_TIME_NANOS, "smartengine.demo.time.nanos"},
};

struct HistogramData {
  double median;
  double percentile95;
  double percentile99;
  double average;
  double standard_deviation;
  // zero-initialize new members since old Statistics::histogramData()
  // implementations won't write them.
  double max = 0.0;
};

enum HistogramsInternal : uint32_t {
  INTERNAL_HISTOGRAM_START = HISTOGRAM_ENUM_MAX,
  INTERNAL_HISTOGRAM_ENUM_MAX
};

class HistogramBucketMapper {
 public:
  HistogramBucketMapper();

  // converts a value to the bucket index.
  size_t IndexForValue(uint64_t value) const;
  // number of buckets required.

  size_t BucketCount() const { return bucketValues_.size(); }

  uint64_t LastValue() const { return maxBucketValue_; }

  uint64_t FirstValue() const { return minBucketValue_; }

  uint64_t BucketLimit(const size_t bucketNumber) const {
    assert(bucketNumber < BucketCount());
    return bucketValues_[bucketNumber];
  }

 private:
  const std::vector<uint64_t> bucketValues_;
  const uint64_t maxBucketValue_;
  const uint64_t minBucketValue_;
  std::map<uint64_t, uint64_t> valueIndexMap_;
};

struct HistogramStat {
  HistogramStat();
  ~HistogramStat() {}

  HistogramStat(const HistogramStat&) = delete;
  HistogramStat& operator=(const HistogramStat&) = delete;

  void Clear();
  bool Empty() const;
  void Add(uint64_t value);
  void add_zero();
  void Merge(const HistogramStat& other);

  inline uint64_t min() const { return min_.load(std::memory_order_relaxed); }
  inline uint64_t max() const { return max_.load(std::memory_order_relaxed); }
  inline uint64_t num() const { return num_.load(std::memory_order_relaxed); }
  inline uint64_t sum() const { return sum_.load(std::memory_order_relaxed); }
  inline uint64_t sum_squares() const {
    return sum_squares_.load(std::memory_order_relaxed);
  }
  inline uint64_t bucket_at(size_t b) const {
    return buckets_[b].load(std::memory_order_relaxed);
  }

  double Median() const;
  double Percentile(double p) const;
  double Average() const;
  double StandardDeviation() const;
  void Data(HistogramData* const data) const;
  std::string ToString() const;
  void to_detail_string(std::string &str) const;

  // To be able to use HistogramStat as thread local variable, it
  // cannot have dynamic allocated member. That's why we're
  // using manually values from BucketMapper
  std::atomic_uint_fast64_t min_;
  std::atomic_uint_fast64_t max_;
  std::atomic_uint_fast64_t num_;
  std::atomic_uint_fast64_t sum_;
  std::atomic_uint_fast64_t sum_squares_;
  std::atomic_uint_fast64_t buckets_[138];  // 138==BucketMapper::BucketCount()
  const uint64_t num_buckets_;
};

struct HistogramStatNonThread {
  HistogramStatNonThread();
  ~HistogramStatNonThread() {}

  HistogramStatNonThread(const HistogramStat&) = delete;
  HistogramStatNonThread& operator=(const HistogramStat&) = delete;

  void Clear();
  bool Empty() const;
  void Add(uint64_t value);
  void add_zero(int32_t count /* how many zero */);
  void Merge(const HistogramStatNonThread& other);

  inline uint64_t min() const { return min_; }
  inline uint64_t max() const { return max_; }
  inline uint64_t num() const { return num_; }
  inline uint64_t sum() const { return sum_; }
  inline uint64_t sum_squares() const {
    return sum_squares_;
  }
  inline uint64_t bucket_at(size_t b) const {
    return buckets_[b];
  }

  double Median() const;
  double Percentile(double p) const;
  double Average() const;
  double StandardDeviation() const;
  void Data(HistogramData* const data) const;
  std::string ToString() const;
  void to_detail_string(std::string &str) const;

  // To be able to use HistogramStat as thread local variable, it
  // cannot have dynamic allocated member. That's why we're
  // using manually values from BucketMapper
  std::uint64_t min_;
  std::uint64_t max_;
  std::uint64_t num_;
  std::uint64_t sum_;
  std::uint64_t sum_squares_;
  uint64_t padding_;
  std::uint64_t buckets_[138];  // 138==BucketMapper::BucketCount()
  const uint64_t num_buckets_;
};

class Histogram {
 public:
  Histogram() {}
  virtual ~Histogram(){}

  virtual void Clear() = 0;
  virtual bool Empty() const = 0;
  virtual void Add(uint64_t value) = 0;
  virtual void add_zero(int32_t count /* how many zero */) = 0;
  virtual void Merge(const Histogram&) = 0;

  virtual std::string ToString() const = 0;
  virtual void to_detail_string(std::string &str) const = 0;
  virtual const char* Name() const = 0;
  virtual uint64_t min() const = 0;
  virtual uint64_t max() const = 0;
  virtual uint64_t num() const = 0;
  virtual double Median() const = 0;
  virtual double Percentile(double p) const = 0;
  virtual double Average() const = 0;
  virtual double StandardDeviation() const = 0;
  virtual void Data(HistogramData* const data) const = 0;
};

class HistogramImpl {
 public:
  HistogramImpl() { Clear(); }

  HistogramImpl(const HistogramImpl&) = delete;
  HistogramImpl& operator=(const HistogramImpl&) = delete;

  void Clear();
  bool Empty() const;
  void Add(uint64_t value);
  void add_zero(int32_t count /* how many zero */);
  void Merge(const Histogram& other);
  void Merge(const HistogramImpl& other);

  std::string ToString() const;
  void to_detail_string(std::string &str) const;
  const char* Name() const { return "HistogramImpl"; }
  uint64_t min() const { return stats_.min(); }
  uint64_t max() const { return stats_.max(); }
  uint64_t num() const { return stats_.num(); }
  double Median() const;
  double Percentile(double p) const;
  double Average() const;
  double StandardDeviation() const;
  void Data(HistogramData* const data) const;

  ~HistogramImpl() {}

 private:
  HistogramStatNonThread stats_;
  std::mutex mutex_;
};

}  // namespace monitor
}  // namespace smartengine
