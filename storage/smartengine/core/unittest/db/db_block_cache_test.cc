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
#include <cstdlib>
#include "cache/lru_cache.h"
#include "db/db_test_util.h"
#include "util/stack_trace.h"
#include "table/extent_table_factory.h"

namespace smartengine {
using namespace common;
using namespace util;
using namespace table;
using namespace cache;
using namespace memtable;
using namespace monitor;
using namespace storage;

namespace db {

class DBBlockCacheTest : public DBTestBase {
 private:
  //TODO(Zhao Dongsheng): fix this unittest later.
  //size_t miss_count_ = 0;
  //size_t hit_count_ = 0;
  //size_t insert_count_ = 0;
  //size_t failure_count_ = 0;
  int64_t compressed_miss_count_ = 0;
  int64_t compressed_hit_count_ = 0;
  int64_t compressed_insert_count_ = 0;
  int64_t compressed_failure_count_ = 0;

 public:
  const int64_t kNumBlocks = 10;
  const int64_t kValueSize = 100;

  DBBlockCacheTest()
      : DBTestBase("/db_block_cache_test"),
        index_add_cnt_(0),
        index_miss_cnt_(0),
        index_hit_cnt_(0),
        index_insert_bytes_(0),
        index_evict_bytes_(0),
        filter_add_cnt_(0),
        filter_miss_cnt_(0),
        filter_hit_cnt_(0),
        filter_insert_bytes_(0),
        filter_evict_bytes_(0),
        cache_data_miss_cnt_(0),
        cache_add_cnt_(0),
        cache_miss_cnt_(0),
        cache_hit_cnt_(0),
        data_insert_bytes_(0){
  }

  BlockBasedTableOptions GetTableOptions() {
    BlockBasedTableOptions table_options;
    // Set a small enough block size so that each key-value get its own block.
    table_options.block_size = 1;
    return table_options;
  }

  Options GetOptions(const BlockBasedTableOptions& table_options) {
    Options options = CurrentOptions();
    options.avoid_flush_during_recovery = false;
    // options.compression = kNoCompression;
    options.statistics = CreateDBStatistics();
    options.table_factory.reset(
        new table::ExtentBasedTableFactory(table_options));
    return options;
  }

  void InitTable(const Options& options) {
    std::string value(kValueSize, 'a');
    for (int64_t i = 0; i < kNumBlocks; i++) {
      ASSERT_OK(Put(ToString(i), value.c_str()));
    }
  }

  void set_trace_count() {
    index_add_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_ADD);
    index_hit_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_HIT);
    index_miss_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_MISS);
    index_insert_bytes_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_BYTES_INSERT);
    index_evict_bytes_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_BYTES_EVICT);

    filter_miss_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_MISS);
    filter_add_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_ADD);
    filter_hit_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_HIT);
    filter_insert_bytes_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_BYTES_INSERT);
    filter_evict_bytes_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_BYTES_EVICT);
    cache_data_miss_cnt_ =  TestGetGlobalCount(CountPoint::BLOCK_CACHE_DATA_MISS);

    cache_add_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD);
    cache_miss_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS);
    cache_hit_cnt_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_HIT);

    data_insert_bytes_ = TestGetGlobalCount(CountPoint::BLOCK_CACHE_DATA_BYTES_INSERT);
  }

  void CheckCacheCounters(const Options& options,
                          int64_t expected_misses,
                          int64_t expected_hits,
                          int64_t expected_inserts,
                          int64_t expected_failures)
  {
    int64_t new_miss_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS);
    int64_t new_hit_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_HIT);
    int64_t new_insert_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD);
    int64_t new_failure_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD_FAILURES);

    ASSERT_EQ(cache_miss_cnt_ + expected_misses, new_miss_count);
    ASSERT_EQ(cache_hit_cnt_ + expected_hits, new_hit_count);
    ASSERT_EQ(cache_add_cnt_ + expected_inserts, new_insert_count);
    set_trace_count();
  }

  void CheckCompressedCacheCounters(const Options& options,
                                   int64_t expected_misses,
                                   int64_t expected_hits,
                                   int64_t expected_inserts,
                                   int64_t expected_failures)
  {
    //int ret = Status::kOk;
    int64_t new_miss_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_MISS);
    int64_t new_hit_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_HIT);
    int64_t new_insert_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_ADD);
    int64_t new_failure_count = TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_ADD_FAILURES);

    ASSERT_EQ(compressed_miss_count_ + expected_misses, new_miss_count);
    ASSERT_EQ(compressed_hit_count_ + expected_hits, new_hit_count);
    ASSERT_EQ(compressed_insert_count_ + expected_inserts, new_insert_count);
    ASSERT_EQ(compressed_failure_count_ + expected_failures, new_failure_count);
    compressed_miss_count_ = new_miss_count;
    compressed_hit_count_ = new_hit_count;
    compressed_insert_count_ = new_insert_count;
    compressed_failure_count_ = new_failure_count;
  }

 public:
  int64_t index_add_cnt_;
  int64_t index_miss_cnt_;
  int64_t index_hit_cnt_;
  int64_t index_insert_bytes_;
  int64_t index_evict_bytes_;
  int64_t filter_add_cnt_;
  int64_t filter_miss_cnt_;
  int64_t filter_hit_cnt_;
  int64_t filter_insert_bytes_;
  int64_t filter_evict_bytes_;
  int64_t cache_data_miss_cnt_;
  int64_t cache_add_cnt_;
  int64_t cache_miss_cnt_;
  int64_t cache_hit_cnt_;
  int64_t data_insert_bytes_;
};

TEST_F(DBBlockCacheTest, TestWithoutCompressedBlockCache)
{
  set_trace_count();
  ReadOptions read_options;
  BlockBasedTableOptions table_options;
  auto options = GetOptions(table_options);
  InitTable(options);
  table_options.cache_index_and_filter_blocks = false;
  std::shared_ptr<Cache> cache = NewLRUCache(0, 0, false);
  table_options.block_cache = cache;
  options.table_factory.reset(
      new table::ExtentBasedTableFactory(table_options));
  Reopen(options);

  schema::TableSchema table_schema;
  table_schema.set_index_id(0);
  table_schema.get_engine_attribute().set_block_size(1);
  ASSERT_OK(modify_table_schema(table_schema));
  std::vector<Iterator *> iterators(kNumBlocks - 1);
  Iterator* iter = nullptr;
  for (int64_t i = 0; i < kNumBlocks; i++) {
    ASSERT_OK(Put(ToString(i), "value"));
  }
  ASSERT_OK(Flush(0));
  // Load blocks into cache.
  // flush to sst
  set_trace_count();
  for (int64_t i = 0; i < kNumBlocks - 1; i++) {
    iter = db_->NewIterator(read_options, db_->DefaultColumnFamily());
    iterators[i] = iter;
    iter->Seek(ToString(i));
    ASSERT_OK(iter->status());
    CheckCacheCounters(options, 1, 0, 1, 0);
  }

  size_t usage = cache->GetUsage();
  ASSERT_LT(0, usage);
  cache->SetCapacity(usage);
  ASSERT_EQ(usage, cache->GetPinnedUsage());

  // Test with strict capacity limit.
  cache->SetStrictCapacityLimit(true);
  iter = db_->NewIterator(read_options, db_->DefaultColumnFamily());
  iter->Seek(ToString(kNumBlocks - 1));
  ASSERT_TRUE(iter->status().IsIncomplete());
  CheckCacheCounters(options, 1, 0, 0, 1);
  MOD_DELETE_OBJECT(Iterator, iter);

  // Release interators and access cache again.
  for (int64_t i = 0; i < kNumBlocks - 1; i++) {
    MOD_DELETE_OBJECT(Iterator, iterators[i]);
    CheckCacheCounters(options, 0, 0, 0, 0);
  }
  ASSERT_EQ(0, cache->GetPinnedUsage());
  for (int64_t i = 0; i < kNumBlocks - 1; i++) {
    iter = db_->NewIterator(read_options, db_->DefaultColumnFamily());
    iter->Seek(ToString(i));
    ASSERT_OK(iter->status());
    CheckCacheCounters(options, 0, 1, 0, 0);
    iterators[i] = iter;
  }
  for (int64_t i = 0; i < kNumBlocks - 1; i++) {
    MOD_DELETE_OBJECT(Iterator, iterators[i]);
  }
}

#ifdef SNAPPY
TEST_F(DBBlockCacheTest, TestWithCompressedBlockCache) {
  ReadOptions read_options;
  auto table_options = GetTableOptions();
  auto options = GetOptions(table_options);
  options.compression = CompressionType::kSnappyCompression;
  InitTable(options);

  std::shared_ptr<Cache> cache = NewLRUCache(0, 0, false);
  std::shared_ptr<Cache> compressed_cache = NewLRUCache(1 << 25, 0, false);
  table_options.block_cache = cache;
  table_options.block_cache_compressed = compressed_cache;
  options.table_factory.reset(
      new table::ExtentBasedTableFactory(table_options));
  Reopen(options);
  RecordCacheCounters(options);

  std::vector<std::unique_ptr<Iterator>> iterators(kNumBlocks - 1);
  Iterator* iter = nullptr;

  // Load blocks into cache.
  for (size_t i = 0; i < kNumBlocks - 1; i++) {
    iter = db_->NewIterator(read_options);
    iter->Seek(ToString(i));
    ASSERT_OK(iter->status());
    CheckCacheCounters(options, 1, 0, 1, 0);
    CheckCompressedCacheCounters(options, 1, 0, 1, 0);
    iterators[i].reset(iter);
  }
  size_t usage = cache->GetUsage();
  ASSERT_LT(0, usage);
  ASSERT_EQ(usage, cache->GetPinnedUsage());
  size_t compressed_usage = compressed_cache->GetUsage();
  ASSERT_LT(0, compressed_usage);
  // Compressed block cache cannot be pinned.
  ASSERT_EQ(0, compressed_cache->GetPinnedUsage());

  // Set strict capacity limit flag. Now block will only load into compressed
  // block cache.
  cache->SetCapacity(usage);
  cache->SetStrictCapacityLimit(true);
  ASSERT_EQ(usage, cache->GetPinnedUsage());
  iter = db_->NewIterator(read_options);
  iter->Seek(ToString(kNumBlocks - 1));
  ASSERT_TRUE(iter->status().IsIncomplete());
  CheckCacheCounters(options, 1, 0, 0, 1);
  CheckCompressedCacheCounters(options, 1, 0, 1, 0);
//  delete iter;
//  iter = nullptr
  MOD_DELETE_OBJECT(Iterator, iter);

  // Clear strict capacity limit flag. This time we shall hit compressed block
  // cache.
  cache->SetStrictCapacityLimit(false);
  iter = db_->NewIterator(read_options);
  iter->Seek(ToString(kNumBlocks - 1));
  ASSERT_OK(iter->status());
  CheckCacheCounters(options, 1, 0, 1, 0);
  CheckCompressedCacheCounters(options, 0, 1, 0, 0);
//  delete iter;
//  iter = nullptr;
  MOD_DELETE_OBJECT(Iterator, iter);
}
#endif  // SNAPPY

// Make sure that when options.block_cache is set, after a new table is
// created its index/filter blocks are added to block cache.
/*
TEST_F(DBBlockCacheTest, IndexAndFilterBlocksOfNewTableAddedToCache) {
  Options options = CurrentOptions();
  // no use , use querytrace
  //options.statistics = CreateDBStatistics();
  BlockBasedTableOptions table_options;
  table_options.cache_index_and_filter_blocks = true;
  options.table_factory.reset(
      new table::ExtentBasedTableFactory(table_options));
  CreateAndReopenWithCF({"pikachu"}, options);
  ASSERT_OK(Put(1, "key", "val"));

  set_trace_count();
  set_tmp_schema(1);
  ASSERT_OK(Flush(1)); // flush

  // index/filter blocks added to block cache right after table creation.
  ASSERT_EQ(index_add_cnt_ + 2, TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_ADD));
  ASSERT_EQ(cache_add_cnt_ + 3, index and data were added
            TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD));

  // Make sure filter block is in cache.
  std::string value;
  ReadOptions ropt;
  // todo : when filter is in use
  //db_->KeyMayExist(ReadOptions(), handles_[1], "key", &value);

  // Miss count should remain the same.
 // ASSERT_EQ(filter_miss_cnt_ + 1, TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_MISS));
 // ASSERT_EQ(filter_hit_cnt_, TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_HIT));

 // db_->KeyMayExist(ReadOptions(), handles_[1], "key", &value);
//  ASSERT_EQ(filter_miss_cnt_ + 3, TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_MISS));
//  ASSERT_EQ(filter_hit_cnt_, TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_HIT));

  // Make sure index block is in cache.
  set_trace_count();
  value = Get(1, "key");
  ASSERT_EQ(index_miss_cnt_, TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_MISS));
  ASSERT_EQ(index_hit_cnt_ + 1,
            TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_HIT));

  value = Get(1, "key");
  ASSERT_EQ(index_miss_cnt_, TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_MISS));
  ASSERT_EQ(index_hit_cnt_+ 2,
            TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_HIT));
}
*/

/*
TEST_F(DBBlockCacheTest, IndexAndFilterBlocksStats) {

  Options options = CurrentOptions();
  options.statistics = CreateDBStatistics();
  BlockBasedTableOptions table_options;
  table_options.cache_index_and_filter_blocks = true;
  // 200 bytes are enough to hold the first two blocks
  std::shared_ptr<Cache> cache = NewLRUCache(200, 0, false);
  table_options.block_cache = cache;
  options.table_factory.reset(
      new table::ExtentBasedTableFactory(table_options));
  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "key", "val"));
  // Create a new table
  set_trace_count();
  set_tmp_schema(1);
  ASSERT_OK(Flush(1));
  size_t index_bytes_insert =
      TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_BYTES_INSERT) - index_insert_bytes_;
  size_t data_bytes_insert =
      TestGetGlobalCount(CountPoint::BLOCK_CACHE_DATA_BYTES_INSERT) - data_insert_bytes_;
//  size_t filter_bytes_insert =
//      TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_BYTES_INSERT);
  ASSERT_GT(index_bytes_insert, 0);
  ASSERT_GT(data_bytes_insert, 0);
//  ASSERT_GT(filter_bytes_insert, 0);
  ASSERT_EQ(cache->GetUsage(), index_bytes_insert + data_bytes_insert);
  // set the cache capacity to the current usage
  cache->SetCapacity(index_bytes_insert + data_bytes_insert);
  ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_BYTES_EVICT) - index_evict_bytes_, 0);
//  ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_BYTES_EVICT), 0);

  ASSERT_OK(Put(1, "key2", "val"));
  // Create a new table
  set_tmp_schema(1);
  ASSERT_OK(Flush(1));
  // cache evicted old index and block entries
  ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_BYTES_INSERT) - index_insert_bytes_,
            index_bytes_insert);
//  ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_BYTES_INSERT) - filter_evict_bytes_,
//            filter_bytes_insert);
//  ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_BYTES_EVICT),
//            filter_bytes_insert);
}
*/

namespace {

// A mock cache wraps LRUCache, and record how many entries have been
// inserted for each priority.
class MockCache : public LRUCache {
 public:
  static uint32_t high_pri_insert_count;
  static uint32_t low_pri_insert_count;

  MockCache() : LRUCache(1 << 25, 0, false, 0.0, 0) {}

  virtual Status Insert(const Slice& key, void* value, size_t charge,
                        void (*deleter)(const Slice& key, void* value),
                        Handle** handle, Priority priority,
                        const bool old,
                        const uint64_t seq,
                        const uint32_t cfd_id) override {
    if (priority == Priority::LOW) {
      low_pri_insert_count++;
    } else {
      high_pri_insert_count++;
    }
    return LRUCache::Insert(key, value, charge, deleter, handle, priority, false, 0, 0);
  }
};

uint32_t MockCache::high_pri_insert_count = 0;
uint32_t MockCache::low_pri_insert_count = 0;

}  // anonymous namespace

TEST_F(DBBlockCacheTest, IndexAndFilterBlocksCachePriority) {

  for (auto priority : {Cache::Priority::LOW, Cache::Priority::HIGH}) {
    Options options = CurrentOptions();
    options.statistics = CreateDBStatistics();
    BlockBasedTableOptions table_options;
    table_options.cache_index_and_filter_blocks = true;
    table_options.block_cache.reset(new MockCache());
    table_options.cache_index_and_filter_blocks_with_high_priority =
        priority == Cache::Priority::HIGH ? true : false;
    options.table_factory.reset(
        new table::ExtentBasedTableFactory(table_options));
    DestroyAndReopen(options);

    MockCache::high_pri_insert_count = 0;
    MockCache::low_pri_insert_count = 0;

    // Create a new table. two minitables
    ASSERT_OK(Put("foo", "value"));
    set_trace_count();
    ASSERT_OK(Flush(0));
    dbfull()->TEST_WaitForFlushMemTable();
    ASSERT_OK(Put("bar", "value"));
    ASSERT_OK(Flush(0));
    dbfull()->TEST_WaitForFlushMemTable();

    // index/filter blocks added to block cache right after table creation.
    // builder.cc: new index
    // The lifecycle of the index block remains consistent with the extent reader.
    // There is currently no scenario where the index block is stored separately in
    // the block cache.
    ASSERT_EQ(index_miss_cnt_ + 0, TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_MISS));
   // ASSERT_EQ(1, TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_MISS));
    // two extents
    // when flush, two data blocks, two index blocks
    // after flush ,when prefetch, two index blocks(above index blocks is no used todo fixed)
    // Now, index block is not added to the block cache anymore.Therefore, the four cache add
    // for index block should be subtracted and the two cache miss for prefetch index block also
    // should be subtracted.
    ASSERT_EQ(cache_add_cnt_ + 2, TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD));
    ASSERT_EQ(cache_miss_cnt_ + 0, TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS));
    if (priority == Cache::Priority::LOW) {
      // 1.data block
      // 2.properties block
      // 3.meta index block
      // 4.index block
      // 5.index reader -> index blocks
      // 6.filter blocks
      ASSERT_EQ(0, MockCache::high_pri_insert_count);
      ASSERT_EQ(2, MockCache::low_pri_insert_count);
    } else {
      ASSERT_EQ(0, MockCache::high_pri_insert_count);
      ASSERT_EQ(2, MockCache::low_pri_insert_count);
    }

    // Access data block.
    ASSERT_EQ("value", Get("foo"));
    ASSERT_EQ("value", Get("bar"));

    ASSERT_EQ(index_miss_cnt_ + 0, TestGetGlobalCount(CountPoint::BLOCK_CACHE_INDEX_MISS));
    //ASSERT_EQ(1, TestGetGlobalCount(CountPoint::BLOCK_CACHE_FILTER_MISS));
    ASSERT_EQ(cache_add_cnt_ + 2, /*adding data block*/ TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD));
    ASSERT_EQ(cache_data_miss_cnt_, TestGetGlobalCount(CountPoint::BLOCK_CACHE_DATA_MISS));

    // Data block should be inserted with low priority.
    if (priority == Cache::Priority::LOW) {
      ASSERT_EQ(0, MockCache::high_pri_insert_count);
      ASSERT_EQ(2, MockCache::low_pri_insert_count);
    } else {
      ASSERT_EQ(0, MockCache::high_pri_insert_count);
      ASSERT_EQ(2, MockCache::low_pri_insert_count);
    }
  }
}

TEST_F(DBBlockCacheTest, ParanoidFileChecks) {
  Options options = CurrentOptions();
  options.statistics = CreateDBStatistics();
  options.level0_file_num_compaction_trigger = 2;
  BlockBasedTableOptions table_options;
  table_options.cache_index_and_filter_blocks = false;
  options.table_factory.reset(
      new table::ExtentBasedTableFactory(table_options));

  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "1_key", "val"));
  ASSERT_OK(Put(1, "9_key", "val"));

  // Create a new table.
  set_trace_count();
  ASSERT_OK(Flush(1));
  dbfull()->TEST_WaitForFlushMemTable(get_column_family_handle(1));
  ASSERT_EQ(cache_add_cnt_ + 1, /* read and cache data block */
            TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD));

  ASSERT_OK(Put(1, "1_key2", "val2"));
  ASSERT_OK(Put(1, "9_key2", "val2"));
  // Create a new SST file. This will further trigger a compaction
  // and generate another file.
  ASSERT_OK(Flush(1));
  dbfull()->TEST_WaitForCompact();
  ASSERT_EQ(cache_add_cnt_ + 2,
            TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD));

  ASSERT_OK(Put(1, "1_key3", "val3"));
  ASSERT_OK(Put(1, "9_key3", "val3"));
  ASSERT_OK(Flush(1));
  ASSERT_OK(Put(1, "1_key4", "val4"));
  ASSERT_OK(Put(1, "9_key4", "val4"));
  ASSERT_OK(Flush(1));
  dbfull()->TEST_WaitForCompact();
  ASSERT_EQ(cache_add_cnt_ + 4, /* Totally 3 files created up to now */
            TestGetGlobalCount(CountPoint::BLOCK_CACHE_ADD));
}

TEST_F(DBBlockCacheTest, CompressedCache) {
  if (!Snappy_Supported()) {
    return;
  }
  int num_iter = 80;

  // Run this test three iterations.
  // Iteration 1: only a uncompressed block cache
  // Iteration 2: only a compressed block cache
  // Iteration 3: both block cache and compressed cache
  // Iteration 4: both block cache and compressed cache, but DB is not
  // compressed
  for (int iter = 0; iter < 4; iter++) {
    Options options = CurrentOptions();
    options.write_buffer_size = 64 * 1024;  // small write buffer
    options.statistics = CreateDBStatistics();

    BlockBasedTableOptions table_options;
    switch (iter) {
      case 0:
        // only uncompressed block cache
        table_options.block_cache = NewLRUCache(8 * 1024);
        table_options.block_cache_compressed = nullptr;
        options.table_factory.reset(
            new table::ExtentBasedTableFactory(table_options));
        break;
      case 1:
        // no block cache, only compressed cache
        table_options.no_block_cache = true;
        table_options.block_cache = nullptr;
        table_options.block_cache_compressed = NewLRUCache(8 * 1024);
        options.table_factory.reset(
            new table::ExtentBasedTableFactory(table_options));
        break;
      case 2:
        // both compressed and uncompressed block cache
        table_options.block_cache = NewLRUCache(1024);
        table_options.block_cache_compressed = NewLRUCache(8 * 1024);
        options.table_factory.reset(
            new table::ExtentBasedTableFactory(table_options));
        break;
      case 3:
        // both block cache and compressed cache, but DB is not compressed
        // also, make block cache sizes bigger, to trigger block cache hits
        table_options.block_cache = NewLRUCache(1024 * 1024);
        table_options.block_cache_compressed = NewLRUCache(8 * 1024 * 1024);
        options.table_factory.reset(
            new table::ExtentBasedTableFactory(table_options));
        break;
      default:
        ASSERT_TRUE(false);
    }
    CreateAndReopenWithCF({"pikachu"}, options);
    // default column family doesn't have block cache
    Options no_block_cache_opts;
    no_block_cache_opts.statistics = options.statistics;
    no_block_cache_opts = CurrentOptions(no_block_cache_opts);
    BlockBasedTableOptions table_options_no_bc;
    table_options_no_bc.no_block_cache = true;
    no_block_cache_opts.table_factory.reset(
        new table::ExtentBasedTableFactory(table_options_no_bc));
    ReopenWithColumnFamilies(
        {"default", "pikachu"},
        std::vector<Options>({no_block_cache_opts, options}));

    Random rnd(301);

    // Write 8MB (80 values, each 100K)
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
    std::vector<std::string> values;
    std::string str;
    for (int i = 0; i < num_iter; i++) {
      if (i % 4 == 0) {  // high compression ratio
        str = RandomString(&rnd, 1000);
      }
      values.push_back(str);
      ASSERT_OK(Put(1, Key(i), values[i]));
    }

    // flush all data from memtable so that reads are from block cache
//    set_tmp_schema(1);
    ASSERT_OK(Flush(1));

    for (int i = 0; i < num_iter; i++) {
      ASSERT_EQ(Get(1, Key(i)), values[i]);
    }

    // check that we triggered the appropriate code paths in the cache
    switch (iter) {
      case 0:
        // only uncompressed block cache
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS), 0);
        ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_MISS), 0);
        break;
      case 1:
        // no block cache, only compressed cache
        ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS), 0);
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_MISS), 0);
        break;
      case 2:
        // both compressed and uncompressed block cache
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS), 0);
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_MISS), 0);
        break;
      case 3:
        // both compressed and uncompressed block cache
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_MISS), 0);
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_HIT), 0);
        ASSERT_GT(TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_MISS), 0);
        // compressed doesn't have any hits since blocks are not compressed on
        // storage
        ASSERT_EQ(TestGetGlobalCount(CountPoint::BLOCK_CACHE_COMPRESSED_HIT), 0);
        break;
      default:
        ASSERT_TRUE(false);
    }

    DestroyAndReopen(options);
  }
}

}
}  // namespace smartengine

int main(int argc, char** argv) {
  smartengine::util::StackTrace::install_handler();
  ::testing::InitGoogleTest(&argc, argv);
  std::string log_path = smartengine::test::TmpDir() + "/db_block_cache_test.log";
  smartengine::logger::Logger::get_log().init(log_path.c_str(), smartengine::logger::INFO_LEVEL);
  return RUN_ALL_TESTS();
}
