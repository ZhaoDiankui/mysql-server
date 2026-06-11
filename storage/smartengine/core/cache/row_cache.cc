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
#include "row_cache.h"
#include "env/env.h"
#include "logger/log_module.h"

namespace smartengine
{
using namespace common;
using namespace memory;
using namespace monitor;

namespace cache
{
static void delete_cache_entry(void *value, void* handler) {
  RowcHandle* cache_value = reinterpret_cast<RowcHandle*>(value);
  RowCacheShard* row_cache = reinterpret_cast<RowCacheShard*>(handler);
  if (nullptr != cache_value && nullptr != row_cache) {
    Slice key = Slice(cache_value->key_data_, cache_value->key_length_);
    row_cache->Erase(key);
  }
}

RowCacheShard::RowCacheShard()
    : capacity_(0),
      strict_capacity_limit_(false),
      usage_(0),
      alloc_()

{
}

RowCacheShard::~RowCacheShard()
{
}

Cache::Handle *RowCacheShard::lookup_row(const common::Slice& key,
                                         const uint64_t snapshot,
                                         const db::ColumnFamilyData *cfd) {
  RowcHandle *handle = nullptr;
  int ret = 0;
  if (FAILED(hash_table_.find(key, handle, true))) {
    SE_LOG(WARN, "failed to find row handle", K(ret));
  } else if (nullptr != handle) {
    handle->alloc_->hit_add();
  }
  return reinterpret_cast<Cache::Handle *>(handle);
}

int RowCacheShard::insert_row(const common::Slice& key,
                              const void* value,
                              const size_t value_size,
                              const uint64_t key_seq,
                              const db::ColumnFamilyData *cfd,
                              const uint64_t seq,
                              Cache::Handle** handle) {
  int ret = 0;
  if (value_size > MAX_ROW_SIZE || key.size() > MAX_ROW_SIZE) {
    return ret;
  }
  ChunkAllocator *allocator = nullptr;
  int64_t handle_size = sizeof(RowcHandle) - 1 + key.size();
  void *buf = alloc_.alloc(handle_size + value_size, allocator);
  if (nullptr == buf) {
    alloc_.evict_one_chunk();
    buf = alloc_.alloc(handle_size + value_size, allocator);
    if (nullptr == buf) {
      // do nothing
      return ret;
    }
  }
  memcpy((char *)buf + handle_size, value, value_size);
  RowcHandle  *rhandle = new (buf) RowcHandle(value_size + handle_size, key.size(), key_seq, allocator);
  memcpy(rhandle->key_data_, key.data(), key.size());
  rhandle->refs_ = 1;
  RowCacheCheck check;
  check.cfd_ = cfd;
  check.seq_ = seq;
  RowcHandle *old_handle = nullptr;
  ret = hash_table_.insert(key, rhandle, &check, old_handle);
  if (FAILED(ret)) {
    allocator->free(buf, rhandle->charge_);
    if (nullptr != handle) {
      *handle = nullptr;
    }
    allocator->set_abort();
  } else {
    usage_ += rhandle->charge_;
    if (nullptr != handle) {
      *handle = (Cache::Handle*)rhandle;
    }
    allocator->set_commit();
  }
  if (nullptr != old_handle) {
    if (unref(old_handle)) {
      usage_ -= old_handle->charge_;
      old_handle->alloc_->free(old_handle, old_handle->charge_);
    }
  }
  if (Status::kInsertCheckFailed == ret) {
    ret = Status::kOk;
  }
  return ret;
}

bool RowCacheShard::check_in_cache(const common::Slice& key) {
  RowcHandle *handle = nullptr;
  int ret = 0;
  if (FAILED(hash_table_.find(key, handle))) {
    SE_LOG(WARN, "failed to find key in hashtable", K(ret), K(key));
  }
  return handle != nullptr;
}

bool RowCacheShard::Ref(Cache::Handle *handle)
{
  RowcHandle* row_handle = reinterpret_cast<RowcHandle*>(handle);
  row_handle->refs_.fetch_add(1);
  return true;
}

bool RowCacheShard::unref(RowcHandle *e) {
  assert(e);
  return 1 == e->refs_.fetch_sub(1);
}

void RowCacheShard::Release(Cache::Handle *handle) {
  if (nullptr == handle) {
    return;
  }
  RowcHandle* row_handle = reinterpret_cast<RowcHandle*>(handle);
  bool last_reference = unref(row_handle);
  if (last_reference) {
    usage_ -= row_handle->charge_;
    row_handle->alloc_->free(row_handle, row_handle->charge_);
  }
}

void* RowCacheShard::Value(Cache::Handle* handle) {
  RowcHandle *rhandle = reinterpret_cast<RowcHandle*>(handle);
  return (void *)((char *)rhandle + sizeof(rhandle) - 1 + rhandle->key_length_);
}

void RowCacheShard::Erase(const Slice &key) {
  RowcHandle *e = nullptr;
  bool last_reference = false;
  int ret = 0;
  if (FAILED(hash_table_.remove(key, e))) {
    SE_LOG(WARN, "failed to remove key from hashtable", K(key));
  } else if (nullptr != e) {
    last_reference = unref(e);
    if (last_reference) {
      usage_ -= e->charge_;
      e->alloc_->free(e, e->charge_);
    }
  }
}

void RowCacheShard::SetCapacity(const int64_t capacity) {
  if (capacity <= capacity_) {
    // not support decrease cap
  } else {
    capacity_ = capacity;
    hash_table_.set_buckets_num(capacity_ / AVG_ROW_LENGTH);
    int64_t hashtable_usage = hash_table_.get_usage();
    int64_t chunk_size = ChunkAllocator::CHUNK_SIZE + memory::AllocMgr::kBlockMeta;
    int64_t chunk_num =  (capacity - hashtable_usage + chunk_size / 2) / chunk_size;
    if (chunk_num < 2) {
      chunk_num = 2;
    }
    alloc_.init(chunk_num, &delete_cache_entry, this);
  }
}

void RowCacheShard::SetStrictCapacityLimit(bool strict_capacity_limit) {
  strict_capacity_limit_ = strict_capacity_limit;
}

bool RowCacheShard::HasStrictCapacityLimit() const
{
  return strict_capacity_limit_;
}

int64_t RowCacheShard::GetCapacity() const
{
  return capacity_;
}

int64_t RowCacheShard::GetUsage() const
{
  int64_t alloc_usage = alloc_.get_usage();
  return usage_;
}

size_t RowCacheShard::GetUsage(Cache::Handle* handle) const
{
  if (nullptr == handle) {
    return 0;
  }
  RowcHandle* row_handle = reinterpret_cast<RowcHandle*>(handle);
  return row_handle->charge_;
}

void RowCacheShard::print_stats(std::string &stat_str) const {
  alloc_.print_chunks_stat(stat_str);
}

const int64_t RowCache::MIN_ROW_CACHE_SHARD_BITS_NUM = 1;
const int64_t RowCache::MAX_ROW_CACHE_SHARD_BITS_NUM = 10;
const int64_t RowCache::MIN_ROW_CACHE_SHARD_SIZE = 8 * 1024 * 1024; //8MB

RowCache::RowCache()
    : is_inited_(false),
      capacity_(0),
      shard_bits_num_(MIN_ROW_CACHE_SHARD_BITS_NUM),
      shards_(nullptr)
{
}

RowCache::~RowCache()
{
  destroy();
}

void RowCache::destroy()
{
  if (is_inited_) {
    for_each_shard([](RowCacheShard *shard) { shard->~RowCacheShard();});
    if (nullptr != shards_) {
      memory::base_free(shards_, false);
    }
    is_inited_ = false;
  }
}

/** Initialize function.
@param  capacity      row cache capacity
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::init(const int64_t capacity)
{
  int ret = Status::kOk;
  int64_t shards_num = 0;

  if (is_inited_) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "RowCache has been inited", K(ret));
  } else if ((0 >= capacity)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "RowCache invalid argument", K(ret), K(capacity));
  } else if (FAILED(init_capacity(capacity))) {
    SE_LOG(WARN, "fail to init capacity", K(ret), K(capacity));
  } else if (FAILED(init_shards())) {
    SE_LOG(WARN, "fail to init row cache shards", K(ret));
  } else {
    is_inited_ = true;
    SE_LOG(INFO, "success to init row cache", K_(capacity), K_(shard_bits_num));
  }

  return ret;
}

/**Insert row into row cache.
@param[in]  key       row cache entry's key
@param[in]  value     value part of kv row
@param[in]  chage     value size
@param[in]  key_seq   sequence of key
@param[in]  cfd       ColumnFamilyData which contains this row
@param[in]  seq       read snapshot
@param[out] handle    row cache handle
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::insert_row(const common::Slice& key,
                         const void* value,
                         const size_t charge,
                         const uint64_t key_seq,
                         const db::ColumnFamilyData *cfd,
                         const uint64_t seq,
                         Cache::Handle** handle)
{
  int ret = Status::kOk;
  RowCacheShard *shard = nullptr;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowCache should been inited first", K(ret));
  } else if (IS_NULL(shard = get_shard_by_key(key))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "row cache shard must not nullptr", K(ret));
  } else if (FAILED(shard->insert_row(key, value, charge, key_seq, cfd, seq, handle))) {
    SE_LOG(WARN, "fail to insert row into row cache shard", K(ret));
  } else {
    //succeed
  }

  return ret;
}

int RowCache::evict(const uint32_t index_id, const common::Slice &key)
{
  int ret = Status::kOk;
  bool in_cache = false;
  db::IterKey cache_key;
  
  // TODO(Zhao Dongsheng) : the user key alreay contains index_id, no need
  // to construct row cache key with index_id.Or we could remove index id
  // from user key to save storage space. 
  // 1. Construct row cache key: {index_id:user_key}
  char buf[10] = {0};
  char *end = util::EncodeVarint32(buf, index_id);
  cache_key.TrimAppend(cache_key.Size(), buf, end - buf);
  Slice user_key = db::ExtractUserKey(key);
  cache_key.TrimAppend(cache_key.Size(), user_key.data(), user_key.size());

  // 2. Erase old version row from row cache
  if (FAILED(check_in_cache(cache_key.GetUserKey(), in_cache))) {
    SE_LOG(WARN, "fail to check key in cache", K(ret));
  } else if (in_cache) {
    erase(cache_key.GetUserKey());
    QUERY_COUNT(CountPoint::ROW_CACHE_EVICT);
  }

  return ret;
}

/** Look up row from row cache.
@param[in]  key         row cache key
@param[in]  snapshot    read snapshot
@param[in]  subtable    ColumnFamilyData which contains this row
@param[out] handle      row cache handle
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::lookup_row(const common::Slice &key, const uint64_t snapshot,
                         const db::ColumnFamilyData *subtable, Cache::Handle *&handle)
{
  int ret = Status::kOk;
  RowCacheShard *shard = nullptr;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowCache should been inited first", K(ret));
  } else if (IS_NULL(shard = get_shard_by_key(key))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "row cache shard must not been nullptr", K(ret));
  } else {
    handle = shard->lookup_row(key, snapshot, subtable);
  }

  return ret;
}

/**Erase row entry from row cache.
@param[in]  key     row cache key
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::erase(const common::Slice &key)
{
  int ret = Status::kOk;
  RowCacheShard *shard = nullptr;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowCache should been inited first", K(ret));
  } else if (IS_NULL(shard = get_shard_by_key(key))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "row cache shard must not been nullptr", K(ret));
  } else {
    shard->Erase(key);
  }

  return ret;
}

/**Relase the refrence of handle.
@param[in]  handle      row cache handle
@return Status::kOK if succeed, or other error code if failed*/
int RowCache::release(Cache::Handle *handle)
{
  int ret = Status::kOk;
  RowCacheShard *shard = nullptr;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowCache should been inited first", K(ret));
  } else if (IS_NULL(handle)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "RowCache invalid argument", K(ret), KP(handle));
  } else if (IS_NULL(shard = get_shard_by_key(common::Slice(
      reinterpret_cast<RowcHandle *>(handle)->key_data_,reinterpret_cast<RowcHandle *>(handle)->key_length_)))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "row cache shard must not been nullptr", K(ret));
  } else {
    shard->Release(handle);
  }

  return ret;
}

/**Check if the key exists in row cache.
@param[in]  key         row cache key
@param[out] in_cache    exist in row cache, or not
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::check_in_cache(const Slice &key, bool &in_cache)
{
  int ret = Status::kOk;
  RowCacheShard *shard = nullptr;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowCache should been inited first", K(ret));
  } else if (IS_NULL(shard = get_shard_by_key(key))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "row cache shard must not been nullptr", K(ret));
  } else {
    in_cache = shard->check_in_cache(key);
  }

  return ret;
}

/**Set row cache's capacity dynamically, the final actual capacity may be
different from the passed capacity, because the size of row cache shard need
not less than MIN_ROW_CACHE_SHARD_SIZE.
@param[in]  capacity      target row cache's capacity
@return Status::kOk if succeed, and other error code if failed*/
int RowCache::set_capacity(const int64_t capacity)
{
  int ret = Status::kOk;
  int64_t actual_capacity = 0;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "RowCache should been inited first", K(ret));
  } else if (MIN_ROW_CACHE_SHARD_SIZE > (actual_capacity = adjust_capacity(capacity))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, the adjusted capacity must not be less than MIN_ROW_CACHE_SHARD_SIZE",
        K(ret), K(actual_capacity));
  } else if (FAILED(set_shards_capacity(actual_capacity))) {
    SE_LOG(WARN, "fail to set capacity low", K(ret), K(capacity));
  } else {
    capacity_ = actual_capacity;
    SE_LOG(INFO, "success to set capacity", "expect_capacity", capacity, "actual_capacity", capacity_);
  }

  return ret;
}

/**Get the total memory usage of row cache handle, according to the size of
row cache handle objects.
@return row cache handle usage*/
int64_t RowCache::get_usage() const
{
  int64_t usage_size = 0;
  const_cast<RowCache *>(this)->for_each_shard(
      [&](RowCacheShard *shard) { usage_size += shard->get_usage(); });

  return usage_size;
}

/**Get the total memory usage of row cache handle, according to the size of
row cache handle objects.
@return row cache handle usage*/
int64_t RowCache::get_allocated_size() const
{
  int64_t allocated_size = 0;
  const_cast<RowCache *>(this)->for_each_shard(
      [&](RowCacheShard *shard) { allocated_size += shard->get_allocated_size(); });

  return allocated_size;
}

/**Schedule async task to evict low priority row cache entries.
@param[in]  env     global resource manager*/
void RowCache::schedule_cache_purge(util::Env *env) {
  if (nullptr != env) {
    env->Schedule(&RowCache::async_cache_purge, this, util::Env::LOW, this);
  }
}

/**Print row cache's statistics.*/
void RowCache::print_stats(std::string &stat_str) const
{
  SE_LOG(INFO, "row cache info", K(get_usage()), K(get_allocated_size()));
}

/**Initialize capacity, include the shard_bits_num.
@param[in]  capacity      expected row cache capacity
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::init_capacity(int64_t capacity)
{
  int ret = Status::kOk;
  int64_t actual_capacity = 0;

  if (0 >= (actual_capacity = adjust_capacity(capacity))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, the actual capacity must not be less than zero", K(ret),
        K(capacity), K(actual_capacity));
  } else if (0 >= (shard_bits_num_ = calculate_shard_bits_num(actual_capacity))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, the row cache shard bits num must not be less than zero", K(ret),
        K(capacity), K(actual_capacity), K_(shard_bits_num));
  } else {
    capacity_ = actual_capacity;
    SE_LOG(INFO, "success to init capacity", K_(capacity), K_(shard_bits_num));
  }

  return ret;
}

/**Initialize row cache shards, construt them and set capacity.
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::init_shards()
{
  int ret = Status::kOk;
  int64_t shards_num = get_shards_num();

  if (IS_NULL(shards_ = static_cast<RowCacheShard *>(
      base_malloc(sizeof(RowCacheShard) * shards_num, ModId::kRowCache)))) {
    ret = Status::kMemoryLimit;
    SE_LOG(INFO, "fail to allocate memory for row cache shards", K(ret), K(shards_num));
  } else if (FAILED(for_each_shard([](RowCacheShard *shard) { new (shard) RowCacheShard(); }))) {
    SE_LOG(WARN, "fail to construct row cache shards", K(ret));
  } else if (FAILED(set_shards_capacity(capacity_))) {
    SE_LOG(WARN, "fail to set row cache shard's capacity", K(ret), K_(capacity));
  } else {
    SE_LOG(INFO, "success to init row cache shards", K(shards_num));
  }

  return ret;
}

/**Get the row cache shard according row cache key.
@[param in] key     row cache key
@return the pointer to row cahce shard*/
RowCacheShard *RowCache::get_shard_by_key(const common::Slice &key)
{
  uint32_t hash_value = util::Hash(key.data(), key.size(), 0);
  uint32_t shard_id = (shard_bits_num_ > 0) ? (hash_value >> (32 - shard_bits_num_)) : 0;

  return get_shard_by_id(shard_id);
}

/**Calculate bit count of row cache shard num according the row cache capacity.
@param[in]  capacity      row cache capacity
@return the bit count of row cache shard num*/
int64_t RowCache::calculate_shard_bits_num(int64_t capacity)
{
  assert(capacity > 0);
  uint64_t shards_num = (capacity + (MIN_ROW_CACHE_SHARD_SIZE - 1)) / MIN_ROW_CACHE_SHARD_SIZE;
  int64_t shard_bits_num = MIN_ROW_CACHE_SHARD_BITS_NUM;

  while (shards_num >>= 1) {
    if (++shard_bits_num >= MAX_ROW_CACHE_SHARD_BITS_NUM) {
      break;
    }
  }

  return shard_bits_num;  
}

/**Set the row cache shard capacity, which set all row cache shards in sequence.
@param[in]  capacity      row cache capacity
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::set_shards_capacity(int64_t capacity)
{
  int ret = Status::kOk;
  int64_t shards_num = get_shards_num();
  int64_t shard_capacity = (capacity + (shards_num - 1)) / shards_num;

  if (FAILED(for_each_shard([&](RowCacheShard *shard) {
      shard->SetCapacity(shard_capacity);}))) {
    SE_LOG(WARN, "fail to set shard capacity", K(ret), K(capacity), K(shard_capacity));
  } else {
    //succeed
  }

  return ret;
}

/**Utility function for execute specific operation on every row cache shard.
@param[func]  func        operation for every row cache shard
@return Status::kOk if succeed, or other error code if failed*/
int RowCache::for_each_shard(ShardFunc &&func)
{
  int ret = Status::kOk;
  int64_t shards_num = get_shards_num();
  RowCacheShard *shard = nullptr;

  for (int32_t shard_id = 0; SUCCED(ret) && shard_id < shards_num; ++shard_id) {
    if (IS_NULL(shard = get_shard_by_id(shard_id))) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "row cache shard must not nullptr", K(ret), K(shard_id), K(shards_num));
    } else {
      func(shard);
    }
  }
  
  return ret;
}

/**Async evit memory chunk of row cahce.*/
void RowCache::async_evict_chunks()
{
  for_each_shard([](RowCacheShard *shard) { shard->async_evict_chunks(); });
}

/**Wrapper of async_evict_chunks.*/
void RowCache::async_cache_purge(void *cache) {
  if (nullptr != cache) {
    reinterpret_cast<RowCache *>(cache)->async_evict_chunks();
  }
}

}
}
