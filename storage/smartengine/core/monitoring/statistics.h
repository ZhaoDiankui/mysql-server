//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "monitoring/histogram.h"
#include "port/port_posix.h"
#include "util/mutexlock.h"
#include "util/thread_local.h"

namespace smartengine {
namespace monitor {

/**
 * Keep adding ticker's here.
 *  1. Any ticker should be added before TICKER_ENUM_MAX.
 *  2. Add a readable string in TickersNameMap below for the newly added ticker.
 */
enum Tickers : uint32_t {
  // total block cache misses
  // REQUIRES: BLOCK_CACHE_MISS == BLOCK_CACHE_INDEX_MISS +
  //                               BLOCK_CACHE_FILTER_MISS +
  //                               BLOCK_CACHE_DATA_MISS;
  BLOCK_CACHE_MISS = 0,
  // total block cache hit
  // REQUIRES: BLOCK_CACHE_HIT == BLOCK_CACHE_INDEX_HIT +
  //                              BLOCK_CACHE_FILTER_HIT +
  //                              BLOCK_CACHE_DATA_HIT;
  BLOCK_CACHE_HIT,
  // # of blocks added to block cache.
  BLOCK_CACHE_ADD,
  // # of failures when adding blocks to block cache.
  BLOCK_CACHE_ADD_FAILURES,
  // # of times cache miss when accessing index block from block cache.
  BLOCK_CACHE_INDEX_MISS,
  // # of times cache hit when accessing index block from block cache.
  BLOCK_CACHE_INDEX_HIT,
  // # of index blocks added to block cache.
  BLOCK_CACHE_INDEX_ADD,
  // # of bytes of index blocks inserted into cache
  BLOCK_CACHE_INDEX_BYTES_INSERT,
  // # of bytes of index block erased from cache
  BLOCK_CACHE_INDEX_BYTES_EVICT,
  // # of times cache miss when accessing filter block from block cache.
  BLOCK_CACHE_FILTER_MISS,
  // # of times cache hit when accessing filter block from block cache.
  BLOCK_CACHE_FILTER_HIT,
  // # of filter blocks added to block cache.
  BLOCK_CACHE_FILTER_ADD,
  // # of bytes of bloom filter blocks inserted into cache
  BLOCK_CACHE_FILTER_BYTES_INSERT,
  // # of bytes of bloom filter block erased from cache
  BLOCK_CACHE_FILTER_BYTES_EVICT,
  // # of times cache miss when accessing data block from block cache.
  BLOCK_CACHE_DATA_MISS,
  // # of times cache hit when accessing data block from block cache.
  BLOCK_CACHE_DATA_HIT,
  // # of data blocks added to block cache.
  BLOCK_CACHE_DATA_ADD,
  // # of bytes of data blocks inserted into cache
  BLOCK_CACHE_DATA_BYTES_INSERT,
  // # of bytes read from cache.
  BLOCK_CACHE_BYTES_READ,
  // # of bytes written into cache.
  BLOCK_CACHE_BYTES_WRITE,

  // # of times bloom filter has avoided file reads.
  BLOOM_FILTER_USEFUL,

  // # persistent cache hit
  PERSISTENT_CACHE_HIT,
  // # persistent cache miss
  PERSISTENT_CACHE_MISS,

  // # total simulation block cache hits
  SIM_BLOCK_CACHE_HIT,
  // # total simulation block cache misses
  SIM_BLOCK_CACHE_MISS,

  // # of memtable hits.
  MEMTABLE_HIT,
  // # of memtable misses.
  MEMTABLE_MISS,

  // # of Get() queries served by L0
  GET_HIT_L0,
  // # of Get() queries served by L1
  GET_HIT_L1,
  // # of Get() queries served by L2 and up
  GET_HIT_L2_AND_UP,

  /**
   * COMPACTION_KEY_DROP_* count the reasons for key drop during compaction
   * There are 4 reasons currently.
   */
  COMPACTION_KEY_DROP_NEWER_ENTRY,  // key was written with a newer value.
                                    // Also includes keys dropped for range del.
  COMPACTION_KEY_DROP_OBSOLETE,     // The key is obsolete.
  COMPACTION_KEY_DROP_USER,  // user compaction function has dropped the key.

  // Number of keys written to the database via the Put and Write call's
  NUMBER_KEYS_WRITTEN,
  // Number of Keys read,
  NUMBER_KEYS_READ,
  // Number keys updated, if inplace update is enabled
  NUMBER_KEYS_UPDATED,
  // The number of uncompressed bytes issued by DB::Put(), DB::Delete(),
  // DB::Merge(), and DB::Write().
  BYTES_WRITTEN,
  // The number of uncompressed bytes read from DB::Get().  It could be
  // either from memtables, cache, or table files.
  // For the number of logical bytes read from DB::MultiGet(),
  // please use NUMBER_MULTIGET_BYTES_READ.
  BYTES_READ,
  // The number of calls to seek/next/prev
  NUMBER_DB_SEEK,
  NUMBER_DB_NEXT,
  NUMBER_DB_PREV,
  // The number of calls to seek/next/prev that returned data
  NUMBER_DB_SEEK_FOUND,
  NUMBER_DB_NEXT_FOUND,
  NUMBER_DB_PREV_FOUND,
  // The number of uncompressed bytes read from an iterator.
  // Includes size of key and value.
  ITER_BYTES_READ,
  NO_FILE_CLOSES,
  NO_FILE_OPENS,
  NO_FILE_ERRORS,
  // DEPRECATED Time system had to wait to do LO-L1 compactions
  STALL_L0_SLOWDOWN_MICROS,
  // DEPRECATED Time system had to wait to move memtable to L1.
  STALL_MEMTABLE_COMPACTION_MICROS,
  // DEPRECATED write throttle because of too many files in L0
  STALL_L0_NUM_FILES_MICROS,
  // Writer has to wait for compaction or flush to finish.
  STALL_MICROS,
  // The wait time for db mutex.
  // Disabled by default. To enable it set stats level to kAll
  DB_MUTEX_WAIT_MICROS,
  RATE_LIMIT_DELAY_MILLIS,
  NO_ITERATORS,  // number of iterators currently open

  // Number of MultiGet calls, keys read, and bytes read
  NUMBER_MULTIGET_CALLS,
  NUMBER_MULTIGET_KEYS_READ,
  NUMBER_MULTIGET_BYTES_READ,

  // Number of deletes records that were not required to be
  // written to storage because key does not exist
  NUMBER_FILTERED_DELETES,
  NUMBER_MERGE_FAILURES,

  // number of times bloom was checked before creating iterator on a
  // file, and the number of times the check was useful in avoiding
  // iterator creation (and thus likely IOPs).
  BLOOM_FILTER_PREFIX_CHECKED,
  BLOOM_FILTER_PREFIX_USEFUL,

  // Number of times we had to reseek inside an iteration to skip
  // over large number of keys with same userkey.
  NUMBER_OF_RESEEKS_IN_ITERATION,

  // Record the number of calls to GetUpadtesSince. Useful to keep track of
  // transaction log iterator refreshes
  GET_UPDATES_SINCE_CALLS,
  BLOCK_CACHE_COMPRESSED_MISS,  // miss in the compressed block cache
  BLOCK_CACHE_COMPRESSED_HIT,   // hit in the compressed block cache
  // Number of blocks added to comopressed block cache
  BLOCK_CACHE_COMPRESSED_ADD,
  // Number of failures when adding blocks to compressed block cache
  BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
  WAL_FILE_SYNCED,  // Number of times WAL sync is done
  WAL_FILE_BYTES,   // Number of bytes written to WAL

  // Writes can be processed by requesting thread or by the thread at the
  // head of the writers queue.
  WRITE_DONE_BY_SELF,
  WRITE_DONE_BY_OTHER,  // Equivalent to writes done for others
  WRITE_TIMEDOUT,       // Number of writes ending up with timed-out.
  WRITE_WITH_WAL,       // Number of Write calls that request WAL
  COMPACT_READ_BYTES,   // Bytes read during compaction
  COMPACT_WRITE_BYTES,  // Bytes written during compaction
  FLUSH_WRITE_BYTES,    // Bytes written during flush

  // Number of table's properties loaded directly from file, without creating
  // table reader object.
  NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
  NUMBER_SUPERVERSION_ACQUIRES,
  NUMBER_SUPERVERSION_RELEASES,
  NUMBER_SUPERVERSION_CLEANUPS,

  // # of compressions/decompressions executed
  NUMBER_BLOCK_COMPRESSED,
  NUMBER_BLOCK_DECOMPRESSED,

  NUMBER_BLOCK_NOT_COMPRESSED,
  MERGE_OPERATION_TOTAL_TIME,
  FILTER_OPERATION_TOTAL_TIME,

  // Row cache.
  ROW_CACHE_HIT,
  ROW_CACHE_MISS,
  ROW_CACHE_ADD,
  ROW_CACHE_EVICT,

  // Read amplification statistics.
  // Read amplification can be calculated using this formula
  // (READ_AMP_TOTAL_READ_BYTES / READ_AMP_ESTIMATE_USEFUL_BYTES)
  //
  // REQUIRES: ReadOptions::read_amp_bytes_per_bit to be enabled
  READ_AMP_ESTIMATE_USEFUL_BYTES,  // Estimate of total bytes actually used.
  READ_AMP_TOTAL_READ_BYTES,       // Total size of loaded data blocks.

  // Number of refill intervals where rate limiter's bytes are fully consumed.
  NUMBER_RATE_LIMITER_DRAINS,

  TICKER_ENUM_MAX
};

// Gauge type represents the current state of the system.
// Currently, we let all the Gauge value be uint64_t.
enum class Gauge : uint64_t {
  // every subtable has its own value, we maintain max value here.
  MAX_LEVEL0_LAYERS = 0,
  MAX_IMM_NUMBERS,
  // internal fragmentation rate
  MAX_LEVEL0_FRAGMENTATION_RATE,
  MAX_LEVEL1_FRAGMENTATION_RATE,
  MAX_LEVEL2_FRAGMENTATION_RATE,
  MAX_LEVEL0_DELETE_PERCENT,
  MAX_LEVEL1_DELETE_PERCENT,
  MAX_LEVEL2_DELETE_PERCENT,
  ALL_FLUSH_MEGABYTES,
  ALL_COMPACTION_MEGABYTES,
  TOP1_SUBTABLE_SIZE,
  TOP2_SUBTABLE_SIZE,
  TOP3_SUBTABLE_SIZE,
  TOP1_MOD_MEM_INFO,
  TOP2_MOD_MEM_INFO,
  TOP3_MOD_MEM_INFO,
  GLOBAL_EXTERNAL_FRAGMENTATION_RATE,

  GAUGE_ENUM_MAX
};

// The order of items listed in  Tickers should be the same as
// the order listed in TickersNameMap
const std::vector<std::pair<Tickers, std::string>> TickersNameMap = {
    {BLOCK_CACHE_MISS, "smartengine.block.cache.miss"},
    {BLOCK_CACHE_HIT, "smartengine.block.cache.hit"},
    {BLOCK_CACHE_ADD, "smartengine.block.cache.add"},
    {BLOCK_CACHE_ADD_FAILURES, "smartengine.block.cache.add.failures"},
    {BLOCK_CACHE_INDEX_MISS, "smartengine.block.cache.index.miss"},
    {BLOCK_CACHE_INDEX_HIT, "smartengine.block.cache.index.hit"},
    {BLOCK_CACHE_INDEX_ADD, "smartengine.block.cache.index.add"},
    {BLOCK_CACHE_INDEX_BYTES_INSERT, "smartengine.block.cache.index.bytes.insert"},
    {BLOCK_CACHE_INDEX_BYTES_EVICT, "smartengine.block.cache.index.bytes.evict"},
    {BLOCK_CACHE_FILTER_MISS, "smartengine.block.cache.filter.miss"},
    {BLOCK_CACHE_FILTER_HIT, "smartengine.block.cache.filter.hit"},
    {BLOCK_CACHE_FILTER_ADD, "smartengine.block.cache.filter.add"},
    {BLOCK_CACHE_FILTER_BYTES_INSERT,
     "smartengine.block.cache.filter.bytes.insert"},
    {BLOCK_CACHE_FILTER_BYTES_EVICT, "smartengine.block.cache.filter.bytes.evict"},
    {BLOCK_CACHE_DATA_MISS, "smartengine.block.cache.data.miss"},
    {BLOCK_CACHE_DATA_HIT, "smartengine.block.cache.data.hit"},
    {BLOCK_CACHE_DATA_ADD, "smartengine.block.cache.data.add"},
    {BLOCK_CACHE_DATA_BYTES_INSERT, "smartengine.block.cache.data.bytes.insert"},
    {BLOCK_CACHE_BYTES_READ, "smartengine.block.cache.bytes.read"},
    {BLOCK_CACHE_BYTES_WRITE, "smartengine.block.cache.bytes.write"},
    {BLOOM_FILTER_USEFUL, "smartengine.bloom.filter.useful"},
    {PERSISTENT_CACHE_HIT, "smartengine.persistent.cache.hit"},
    {PERSISTENT_CACHE_MISS, "smartengine.persistent.cache.miss"},
    {SIM_BLOCK_CACHE_HIT, "smartengine.sim.block.cache.hit"},
    {SIM_BLOCK_CACHE_MISS, "smartengine.sim.block.cache.miss"},
    {MEMTABLE_HIT, "smartengine.memtable.hit"},
    {MEMTABLE_MISS, "smartengine.memtable.miss"},
    {GET_HIT_L0, "smartengine.l0.hit"},
    {GET_HIT_L1, "smartengine.l1.hit"},
    {GET_HIT_L2_AND_UP, "smartengine.l2andup.hit"},
    {COMPACTION_KEY_DROP_NEWER_ENTRY, "smartengine.compaction.key.drop.new"},
    {COMPACTION_KEY_DROP_OBSOLETE, "smartengine.compaction.key.drop.obsolete"},
    {COMPACTION_KEY_DROP_USER, "smartengine.compaction.key.drop.user"},
    {NUMBER_KEYS_WRITTEN, "smartengine.number.keys.written"},
    {NUMBER_KEYS_READ, "smartengine.number.keys.read"},
    {NUMBER_KEYS_UPDATED, "smartengine.number.keys.updated"},
    {BYTES_WRITTEN, "smartengine.bytes.written"},
    {BYTES_READ, "smartengine.bytes.read"},
    {NUMBER_DB_SEEK, "smartengine.number.db.seek"},
    {NUMBER_DB_NEXT, "smartengine.number.db.next"},
    {NUMBER_DB_PREV, "smartengine.number.db.prev"},
    {NUMBER_DB_SEEK_FOUND, "smartengine.number.db.seek.found"},
    {NUMBER_DB_NEXT_FOUND, "smartengine.number.db.next.found"},
    {NUMBER_DB_PREV_FOUND, "smartengine.number.db.prev.found"},
    {ITER_BYTES_READ, "smartengine.db.iter.bytes.read"},
    {NO_FILE_CLOSES, "smartengine.no.file.closes"},
    {NO_FILE_OPENS, "smartengine.no.file.opens"},
    {NO_FILE_ERRORS, "smartengine.no.file.errors"},
    {STALL_L0_SLOWDOWN_MICROS, "smartengine.l0.slowdown.micros"},
    {STALL_MEMTABLE_COMPACTION_MICROS, "smartengine.memtable.compaction.micros"},
    {STALL_L0_NUM_FILES_MICROS, "smartengine.l0.num.files.stall.micros"},
    {STALL_MICROS, "smartengine.stall.micros"},
    {DB_MUTEX_WAIT_MICROS, "smartengine.db.mutex.wait.micros"},
    {RATE_LIMIT_DELAY_MILLIS, "smartengine.rate.limit.delay.millis"},
    {NO_ITERATORS, "smartengine.num.iterators"},
    {NUMBER_MULTIGET_CALLS, "smartengine.number.multiget.get"},
    {NUMBER_MULTIGET_KEYS_READ, "smartengine.number.multiget.keys.read"},
    {NUMBER_MULTIGET_BYTES_READ, "smartengine.number.multiget.bytes.read"},
    {NUMBER_FILTERED_DELETES, "smartengine.number.deletes.filtered"},
    {NUMBER_MERGE_FAILURES, "smartengine.number.merge.failures"},
    {BLOOM_FILTER_PREFIX_CHECKED, "smartengine.bloom.filter.prefix.checked"},
    {BLOOM_FILTER_PREFIX_USEFUL, "smartengine.bloom.filter.prefix.useful"},
    {NUMBER_OF_RESEEKS_IN_ITERATION, "smartengine.number.reseeks.iteration"},
    {GET_UPDATES_SINCE_CALLS, "smartengine.getupdatessince.calls"},
    {BLOCK_CACHE_COMPRESSED_MISS, "smartengine.block.cachecompressed.miss"},
    {BLOCK_CACHE_COMPRESSED_HIT, "smartengine.block.cachecompressed.hit"},
    {BLOCK_CACHE_COMPRESSED_ADD, "smartengine.block.cachecompressed.add"},
    {BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
     "smartengine.block.cachecompressed.add.failures"},
    {WAL_FILE_SYNCED, "smartengine.wal.synced"},
    {WAL_FILE_BYTES, "smartengine.wal.bytes"},
    {WRITE_DONE_BY_SELF, "smartengine.write.self"},
    {WRITE_DONE_BY_OTHER, "smartengine.write.other"},
    {WRITE_TIMEDOUT, "smartengine.write.timeout"},
    {WRITE_WITH_WAL, "smartengine.write.wal"},
    {COMPACT_READ_BYTES, "smartengine.compact.read.bytes"},
    {COMPACT_WRITE_BYTES, "smartengine.compact.write.bytes"},
    {FLUSH_WRITE_BYTES, "smartengine.flush.write.bytes"},
    {NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
     "smartengine.number.direct.load.table.properties"},
    {NUMBER_SUPERVERSION_ACQUIRES, "smartengine.number.superversion_acquires"},
    {NUMBER_SUPERVERSION_RELEASES, "smartengine.number.superversion_releases"},
    {NUMBER_SUPERVERSION_CLEANUPS, "smartengine.number.superversion_cleanups"},
    {NUMBER_BLOCK_COMPRESSED, "smartengine.number.block.compressed"},
    {NUMBER_BLOCK_DECOMPRESSED, "smartengine.number.block.decompressed"},
    {NUMBER_BLOCK_NOT_COMPRESSED, "smartengine.number.block.not_compressed"},
    {MERGE_OPERATION_TOTAL_TIME, "smartengine.merge.operation.time.nanos"},
    {FILTER_OPERATION_TOTAL_TIME, "smartengine.filter.operation.time.nanos"},
    {ROW_CACHE_HIT, "smartengine.row.cache.hit"},
    {ROW_CACHE_MISS, "smartengine.row.cache.miss"},
    {ROW_CACHE_EVICT, "smartengine.row.cache.evict"},
    {READ_AMP_ESTIMATE_USEFUL_BYTES, "smartengine.read.amp.estimate.useful.bytes"},
    {READ_AMP_TOTAL_READ_BYTES, "smartengine.read.amp.total.read.bytes"},
    {NUMBER_RATE_LIMITER_DRAINS, "smartengine.number.rate_limiter.drains"},
};

enum TickersInternal : uint32_t {
  INTERNAL_TICKER_ENUM_START = TICKER_ENUM_MAX,
  INTERNAL_TICKER_ENUM_MAX
};

enum StatsLevel {
  // Collect all stats except time inside mutex lock AND time spent on
  // compression.
  kExceptDetailedTimers,
  // Collect all stats except the counters requiring to get time inside the
  // mutex lock.
  kExceptTimeForMutex,
  // Collect all stats, including measuring duration of mutex operations.
  // If getting time is expensive on the platform to run, it can
  // reduce scalability to more threads, especially for writes.
  All,
};

// Analyze the performance of a db
class Statistics {
 public:
  virtual ~Statistics() {}

  virtual uint64_t getTickerCount(uint32_t tickerType) const = 0;
  virtual void histogramData(uint32_t type,
                             HistogramData* const data) const = 0;
  virtual std::string getHistogramString(uint32_t type) const { return ""; }
  virtual void recordTick(uint32_t tickerType, uint64_t count = 0) = 0;
  virtual void setTickerCount(uint32_t tickerType, uint64_t count) = 0;
  virtual uint64_t getAndResetTickerCount(uint32_t tickerType) = 0;
  virtual void measureTime(uint32_t histogramType, uint64_t time) = 0;
  virtual void set_gauge_value(Gauge which, uint64_t value) = 0;
  virtual uint64_t get_gauge_value(Gauge which) const = 0;
  virtual void update_global_flush_stat(uint64_t bytes_written,
                                        uint64_t start_micros,
                                        uint64_t end_micros) = 0;
  virtual uint64_t get_global_flush_megabytes_written() const = 0;
  virtual void reset_global_flush_stat() = 0;
  virtual void update_global_compaction_stat(uint64_t bytes_written,
                                             uint64_t start_micros,
                                             uint64_t end_micros) = 0;
  virtual uint64_t get_global_compaction_megabytes_written() const = 0;
  virtual void reset_global_compaction_stat() = 0;

  // String representation of the statistic object.
  virtual std::string ToString() const {
    // Do nothing by default
    return std::string("ToString(): not implemented");
  }

  // Override this function to disable particular histogram collection
  virtual bool HistEnabledForType(uint32_t type) const {
    return type < HISTOGRAM_ENUM_MAX;
  }

  StatsLevel stats_level_ = kExceptDetailedTimers;
};

class StatisticsImpl : public Statistics {
 public:
  StatisticsImpl(std::shared_ptr<Statistics> stats, bool enable_internal_stats);
  virtual ~StatisticsImpl() override;

  virtual uint64_t getTickerCount(uint32_t ticker_type) const override;
  virtual void histogramData(uint32_t histogram_type,
                             HistogramData* const data) const override;
  std::string getHistogramString(uint32_t histogram_type) const override;

  virtual void setTickerCount(uint32_t ticker_type, uint64_t count) override;
  virtual uint64_t getAndResetTickerCount(uint32_t ticker_type) override;
  virtual void recordTick(uint32_t ticker_type, uint64_t count) override;
  virtual void measureTime(uint32_t histogram_type, uint64_t value) override;
  void set_gauge_value(Gauge which, uint64_t value) override;
  uint64_t get_gauge_value(Gauge which) const override;
  void update_global_compaction_stat(
      uint64_t bytes_written, [[maybe_unused]] uint64_t start_micros,
      [[maybe_unused]] uint64_t end_micros) override {
    if (bytes_written > 0) {
      compaction_bytes_written_.fetch_add(bytes_written,
                                          std::memory_order_relaxed);
    }
  }

  virtual std::string ToString() const override;
  virtual bool HistEnabledForType(uint32_t type) const override;
  uint64_t get_global_flush_megabytes_written() const override {
    constexpr uint64_t kBytes2Megabytes = 1 << 20;
    return flush_bytes_written_.load(std::memory_order_relaxed) /
           kBytes2Megabytes;
  }

  void reset_global_flush_stat() override {
    flush_bytes_written_.store(0, std::memory_order_relaxed);
  }

  void update_global_flush_stat(uint64_t bytes_written,
                                [[maybe_unused]] uint64_t start_micros,
                                [[maybe_unused]] uint64_t end_micros) override {
    if (bytes_written > 0) {
      flush_bytes_written_.fetch_add(bytes_written, std::memory_order_relaxed);
    }
  }

  uint64_t get_global_compaction_megabytes_written() const override {
    constexpr uint64_t kBytes2Megabytes = 1 << 20;
    return compaction_bytes_written_.load(std::memory_order_relaxed) /
           kBytes2Megabytes;
  }

  void reset_global_compaction_stat() override {
    compaction_bytes_written_.store(0, std::memory_order_relaxed);
  }

 private:
  std::shared_ptr<Statistics> stats_shared_;
  Statistics* stats_;
  bool enable_internal_stats_;
  // Synchronizes setTickerCount()/getTickerCount() operations so partially
  // completed setTickerCount() won't be visible.
  mutable port::Mutex aggregate_lock_;

  // Holds data maintained by each thread for implementing tickers.
  struct ThreadTickerInfo {
    std::atomic_uint_fast64_t value;
    // During teardown, value will be summed into *merged_sum.
    std::atomic_uint_fast64_t* merged_sum;

    ThreadTickerInfo(uint_fast64_t _value,
                     std::atomic_uint_fast64_t* _merged_sum)
        : value(_value), merged_sum(_merged_sum) {}
  };

  // Holds data maintained by each thread for implementing histograms.
  struct ThreadHistogramInfo {
    HistogramImpl value;
    // During teardown, value will be merged into *merged_hist while holding
    // *merge_lock, which also syncs with the merges necessary for reads.
    HistogramImpl* merged_hist;
    port::Mutex* merge_lock;

    ThreadHistogramInfo(HistogramImpl* _merged_hist, port::Mutex* _merge_lock)
        : value(), merged_hist(_merged_hist), merge_lock(_merge_lock) {}
  };

  // Holds global data for implementing tickers.
  struct TickerInfo {
    TickerInfo()
        : thread_value(new util::ThreadLocalPtr(&mergeThreadValue)),
          merged_sum(0) {}
    // Holds thread-specific pointer to ThreadTickerInfo
    std::unique_ptr<util::ThreadLocalPtr> thread_value;
    // Sum of thread-specific values for tickers that have been reset due to
    // thread termination or ThreadLocalPtr destruction. Also, this is used by
    // setTickerCount() to conveniently change the global value by setting this
    // while simultaneously zeroing all thread-local values.
    std::atomic_uint_fast64_t merged_sum;

    static void mergeThreadValue(void* ptr) {
      auto info_ptr = static_cast<ThreadTickerInfo*>(ptr);
      *info_ptr->merged_sum += info_ptr->value;
      delete info_ptr;
    }
  };

  // Holds global data for implementing histograms.
  struct HistogramInfo {
    HistogramInfo()
        : merged_hist(),
          merge_lock(false),
          thread_value(new util::ThreadLocalPtr(&mergeThreadValue)) {}
    // Merged thread-specific values for histograms that have been reset due to
    // thread termination or ThreadLocalPtr destruction. Note these must be
    // destroyed after thread_value since its destructor accesses them.
    HistogramImpl merged_hist;
    mutable port::Mutex merge_lock;
    // Holds thread-specific pointer to ThreadHistogramInfo
    std::unique_ptr<util::ThreadLocalPtr> thread_value;

    static void mergeThreadValue(void* ptr) {
      auto info_ptr = static_cast<ThreadHistogramInfo*>(ptr);
      {
        util::MutexLock lock(info_ptr->merge_lock);
        info_ptr->merged_hist->Merge(info_ptr->value);
      }
      delete info_ptr;
    }

    // Returns a histogram that merges all histograms (thread-specific and
    // previously merged ones).
    std::unique_ptr<HistogramImpl> getMergedHistogram() const;
  };

  // Returns the info for this tickerType/thread. It sets a new info with zeroed
  // counter if none exists.
  ThreadTickerInfo* getThreadTickerInfo(uint32_t ticker_type);
  // Returns the info for this histogramType/thread. It sets a new histogram
  // with zeroed data if none exists.
  ThreadHistogramInfo* getThreadHistogramInfo(uint32_t histogram_type);

  TickerInfo tickers_[INTERNAL_TICKER_ENUM_MAX];
  HistogramInfo histograms_[INTERNAL_HISTOGRAM_ENUM_MAX];
  // we use an asynchronous thread to set/get, no synchronization needed here.
  // Gauge type is actually uint64_t, the subscript of vector means which Gauge
  // metrics.
  std::vector<uint64_t> gauge_metrics_;
  // maintain compaction_bytes_written in monitor interval
  std::atomic<uint64_t> compaction_bytes_written_;
  // maintain flush_bytes_written in monitor interval
  std::atomic<uint64_t> flush_bytes_written_;
};

// Utility functions
inline void MeasureTime(Statistics* statistics, uint32_t histogram_type,
                        uint64_t value) {
  if (statistics) {
    statistics->measureTime(histogram_type, value);
  }
}

inline void RecordTick(Statistics* statistics, uint32_t ticker_type,
                       uint64_t count = 1) {
  if (statistics) {
    statistics->recordTick(ticker_type, count);
  }
}

inline void SetTickerCount(Statistics* statistics, uint32_t ticker_type,
                           uint64_t count) {
  if (statistics) {
    statistics->setTickerCount(ticker_type, count);
  }
}

// Create a concrete DBStatistics object
std::shared_ptr<Statistics> CreateDBStatistics();

}  // namespace monitor
}  // namespace smartengine
