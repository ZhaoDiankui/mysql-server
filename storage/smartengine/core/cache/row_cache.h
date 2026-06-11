/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include "memory/chunk_allocator.h"
#include "db/column_family.h"
#include "util/hash.h"
#include "util/hashtable.h"

namespace smartengine
{
namespace cache
{

struct RowcHandle {
  RowcHandle(const uint32_t charge,
             const uint16_t key_length,
             const uint64_t key_seq,
             memory::ChunkAllocator *alloc = nullptr)
      : charge_(charge),
        refs_(0),
        seq_(key_seq),
        alloc_(alloc),
        key_length_(key_length) {}
  void ref() {
    assert(alloc_);
    refs_.fetch_add(1);
  }
  uint32_t charge_;
  std::atomic<uint32_t> refs_;
  uint64_t seq_;
  memory::ChunkAllocator *alloc_;
  uint32_t key_length_;
  char key_data_[1];
};

template <class HandleType>
struct HandleKey
{
  const common::Slice operator() (const HandleType *handle) const {
    assert(handle);
    return common::Slice(handle->key_data_, handle->key_length_);
  }
};

struct HashSlice
{
  uint32_t operator() (const common::Slice& s) const {
    return util::Hash(s.data(), s.size(), 0);
  }
};

struct EqualSlice
{
  bool operator() (const common::Slice& a, const common::Slice& b) const {
    return a == b;
  }
};

struct RowCacheCheck {
  RowCacheCheck():
    seq_(0),
    cfd_(nullptr) {}
  uint64_t seq_;
  const db::ColumnFamilyData *cfd_;
};

struct CheckFunc {
  bool operator() (const RowCacheCheck* check) const {
    uint64_t end_seq = check->cfd_->get_range_end();
    if (0 != end_seq/*not init*/
        && check->seq_ <= end_seq) {
      // if current seq maybe in flush process,not add it into cache
      // because it maybe become one old version data
      return false;
    }
    return true;
  }
};

class RowCacheShard {
  static const size_t MAX_ROW_SIZE = 4 * 1024;
  static const size_t AVG_ROW_LENGTH = 512;
public:
  RowCacheShard();
  ~RowCacheShard();
  struct SeqRange {
    SeqRange() : start_seq_(0), end_seq_(0) {}
    SeqRange(uint64_t start_seq, uint64_t end_seq)
        : start_seq_(start_seq), end_seq_(end_seq) {}
    uint64_t start_seq_;
    uint64_t end_seq_;
  };

  // The type of the Cache
  const char* Name() const {
    return "";
  }

  Cache::Handle* lookup_row(const common::Slice& key,
                            const uint64_t snapshot = 0,
                            const db::ColumnFamilyData *cfd = nullptr);

  bool check_in_cache(const common::Slice& key);

  bool Ref(Cache::Handle* handle);

  void Release(Cache::Handle* handle);

  void* Value(Cache::Handle* handle);

  void Erase(const common::Slice& key);

  void SetCapacity(const int64_t capacity);

  void SetStrictCapacityLimit(bool strict_capacity_limit);


  bool HasStrictCapacityLimit() const;

  // returns the maximum configured capacity of the cache
  int64_t GetCapacity() const;

  // returns the memory size for the entries residing in the cache.
  int64_t GetUsage() const;
  int64_t get_usage() const {
    return alloc_.get_usage();
  }
  int64_t get_allocated_size() const {
    return alloc_.get_allocated_size();
  }
  // returns the memory size for a specific entry in the cache.
  size_t GetUsage(Cache::Handle* handle) const;

  void DisownData() {
    alloc_.disown_data();
  }

  int insert_row(const common::Slice& key,
                 const void* value,
                 const size_t charge,
                 const uint64_t key_seq,
                 const db::ColumnFamilyData *cfd,
                 const uint64_t seq = 0,
                 Cache::Handle** handle = nullptr);
  bool unref(RowcHandle *e);

  void async_evict_chunks() {
    alloc_.async_evict_chunks();
  }
  void print_stats(std::string &stat_str) const;
private:
  int64_t capacity_;
  bool strict_capacity_limit_;
  int64_t usage_;
  port::Mutex mutex_;
  // for find row in cache
  util::HashTable<common::Slice, RowcHandle *, HashSlice,
  HandleKey<RowcHandle>, CheckFunc, RowCacheCheck *, EqualSlice> hash_table_;
  memory::ChunkManager alloc_; // for object alloc
};

/**RowCache buffers data with row grain, which can be thought of as a global
hashmap. The key is called as row cache key, and the value is called as row
cache handle.And, the format are:

row cache key: | index_id | user key |
row cache handle: | row cache key | sequence | row value | */
class RowCache {
public:
  RowCache();
  ~RowCache();
  void destroy();

  /** Initialize function.
  @param  capacity      row cache capacity
  @return Status::kOk if succeed, or other error code if failed*/
  int init(const int64_t capacity);

  /**Insert row into row cache.
  @param[in]  key       row cache entry's key
  @param[in]  value     value part of kv row
  @param[in]  chage     value size
  @param[in]  key_seq   sequence of key
  @param[in]  cfd       ColumnFamilyData which contains this row
  @param[in]  seq       read snapshot
  @param[out] handle    row cache handle
  @return Status::kOk if succeed, or other error code if failed*/
  int insert_row(const common::Slice& key,
                 const void* value,
                 const size_t charge,
                 const uint64_t key_seq,
                 const db::ColumnFamilyData *cfd,
                 const uint64_t seq = 0,
                 Cache::Handle** handle = nullptr);

  /**Evict row from row cache.
  @param[in] index_id  id of the index to which key BeforeMarkLogsSynced
  @param[in] key  internal key*/
  int evict(const uint32_t index_id, const common::Slice &key);

  /** Look up row from row cache.
  @param[in]  key         row cache key
  @param[in]  snapshot    read snapshot
  @param[in]  subtable    ColumnFamilyData which contains this row
  @param[out] handle      row cache handle
  @return Status::kOk if succeed, or other error code if failed*/
  int lookup_row(const common::Slice &key, const uint64_t snapshot,
                 const db::ColumnFamilyData *subtable, Cache::Handle *&handle);

  /**Erase row entry from row cache.
  @param[in]  key     row cache key
  @return Status::kOk if succeed, or other error code if failed*/
  int erase(const common::Slice &key);

  /**Relase the refrence of handle.
  @param[in]  handle      row cache handle
  @return Status::kOK if succeed, or other error code if failed*/
  int release(Cache::Handle *handle);

  /**Check if the key exists in row cache.
  @param[in]  key         row cache key
  @param[out] in_cache    exist in row cache, or not
  @return Status::kOk if succeed, or other error code if failed*/
  int check_in_cache(const common::Slice &key, bool &in_cache);

  /**Schedule async task to evict low priority row cache entries.
  @param[in]  env     global resource manager*/
  void schedule_cache_purge(util::Env *env);

  /**Set row cache's capacity dynamically, the final actual capacity may be
  different from the passed capacity, because the size of row cache shard need
  not less than MIN_ROW_CACHE_SHARD_SIZE.
  @param[in]  capacity      target row cache's capacity
  @return Status::kOk if succeed, and other error code if failed*/
  int set_capacity(const int64_t capacity);

  /**Get the row cache capacity
  return the capacity*/
  inline int64_t get_capacity() const { return capacity_; }

  /**Get the total memory usage of row cache handle, according to the size of
  row cache handle objects.
  @return row cache handle usage*/
  int64_t get_usage() const;

  /**Get the total allocated memory for row cache handle, according the size of
  allocators.
  @return the size of memory has been allocated by allocators*/
  int64_t get_allocated_size() const;

  /**Print row cache's statistics.*/
  void print_stats(std::string &stat_str) const;

private:
  using ShardFunc = std::function<void (RowCacheShard *)>;

  static const int64_t MIN_ROW_CACHE_SHARD_SIZE;

  static const int64_t MIN_ROW_CACHE_SHARD_BITS_NUM;

  static const int64_t MAX_ROW_CACHE_SHARD_BITS_NUM;

private:
  /**Initialize capacity, include the shard_bits_num.
  @param[in]  capacity      expected row cache capacity
  @return Status::kOk if succeed, or other error code if failed*/
  int init_capacity(int64_t capacity);

  /**Initialize row cache shards, construt them and set capacity.
  @return Status::kOk if succeed, or other error code if failed*/
  int init_shards();

  /**Get the row cache shard according shard id.
  @return the pointer to row cache shard*/
  inline RowCacheShard *get_shard_by_id(int64_t shard_id) { return shards_ + shard_id; }

  /**Get the row cache shard according row cache key.
  @[param in] key     row cache key
  @return the pointer to row cahce shard*/
  inline RowCacheShard *get_shard_by_key(const common::Slice &key);

  /**Get the row cache shards number.
  @return the row cache shard number*/
  inline int64_t get_shards_num() const { return 1LL << shard_bits_num_; }

  /**Adjust the capacity to meet the minimum size requirement of single row cahce shard.
  @return the capacity satifies the requirement*/
  inline int64_t adjust_capacity(int64_t capacity) { return std::max(capacity, get_shards_num() * MIN_ROW_CACHE_SHARD_SIZE); }

  /**Calculate bit count of row cache shard num according the row cache capacity.
  @param[in]  capacity      row cache capacity
  @return the bit count of row cache shard num*/
  inline int64_t calculate_shard_bits_num(int64_t capacity);

  /**Set the row cache shard capacity, which set all row cache shards in sequence.
  @param[in]  capacity      row cache capacity
  @return Status::kOk if succeed, or other error code if failed*/
  int set_shards_capacity(int64_t capacity);

  /**Utility function for execute specific operation on every row cache shard.
  @param[func]  func        operation for every row cache shard
  @return Status::kOk if succeed, or other error code if failed*/
  int for_each_shard(ShardFunc &&func);

  /**Async evit memory chunk of row cahce.*/
  void async_evict_chunks();

  /**Wrapper of async_evict_chunks.*/
  static void async_cache_purge(void *cache);

private:
  /**initialize flag*/
  bool is_inited_;
  /**row cache capacity*/
  int64_t capacity_;
  /**bits num of shards count*/
  int64_t shard_bits_num_;
  /**row cache shard array*/
  RowCacheShard* shards_;
};

}
}
