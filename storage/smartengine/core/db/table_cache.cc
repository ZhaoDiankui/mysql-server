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

#include "db/table_cache.h"
#include "db/dbformat.h"

#include "monitoring/query_perf_context.h"
#include "storage/extent_space_manager.h"
#include "table/extent_reader.h"
#include "table/extent_table_factory.h"
#include "table/get_context.h"
#include "table/internal_iterator.h"

namespace smartengine {
using namespace cache;
using namespace common;
using namespace memory;
using namespace monitor;
using namespace storage;
using namespace table;
using namespace util;

namespace db {
namespace {

template <class T>
static void DeleteEntry(const Slice& key, void* value) {
  T* typed_value = reinterpret_cast<T*>(value);
//  delete typed_value;
  MOD_DELETE_OBJECT(T, typed_value);
}

static void UnrefEntry(void* arg1, void* arg2)
{
  Cache* cache = reinterpret_cast<Cache*>(arg1);
  Cache::Handle* h = reinterpret_cast<Cache::Handle*>(arg2);
  cache->Release(h);
}

static Slice GetSliceForFileNumber(const uint64_t* file_number) {
  return Slice(reinterpret_cast<const char*>(file_number),
               sizeof(*file_number));
}

}  // namespace

TableCache::TableCache(const ImmutableCFOptions& ioptions, Cache* const cache)
    : ioptions_(ioptions),
      cache_(cache)
{
  se_assert(nullptr != cache_);
}

TableCache::~TableCache() {}

InternalIterator* TableCache::create_iterator(const ReadOptions& options,
                                              const InternalKeyComparator& internal_key_comparator,
                                              int32_t level,
                                              const ExtentId &extent_id,
                                              ExtentReader *&extent_reader)
{
  int ret = Status::kOk;
  Cache::Handle* handle = nullptr;
  InternalIterator* iterator = nullptr;

  if (FAILED(find_extent_reader(internal_key_comparator,
                               extent_id,
                               options.read_tier == kBlockCacheTier /*no_io=*/,
                               false /*skip_filters*/,
                               level,
                               true /*prefetch_index_and_filter_in_cache*/,
                               &handle))) {
    SE_LOG(WARN, "fail to find reader handle", K(ret), K(extent_id));
  } else if (IS_NULL(extent_reader = get_extent_reader_from_handle(handle))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "the reader must not be nullptr", K(ret));
  } else if (IS_NULL(iterator = extent_reader->NewIterator(options,
                                                           nullptr /*arena*/,
                                                           false /*skip_filters*/,
                                                           0 /*scan_add_blocks_limit*/))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "fail to create interator", K(ret));
  } else {
    if (IS_NOTNULL(handle)) {
      iterator->RegisterCleanup(&UnrefEntry, cache_, handle);
      handle = nullptr;
    }
  }


  // resource clean
  if (FAILED(ret)) {
    if (IS_NOTNULL(handle)) {
      release_handle(handle);
      handle = nullptr;
    }

    if (IS_NOTNULL(iterator)) {
      MOD_DELETE_OBJECT(InternalIterator, iterator);
      iterator = nullptr;
    }
  } else {
    se_assert(IS_NULL(handle) && IS_NOTNULL(iterator));
  }

  return iterator;
}

int TableCache::get(const ReadOptions& options,
                    const InternalKeyComparator& internal_key_comparator,
                    const int32_t level,
                    const ExtentId &extent_id,
                    const Slice& key,
                    GetContext* get_context)
{
  int ret = Status::kOk;
  ExtentReader* reader = nullptr;
  Cache::Handle* handle = nullptr;

  if (FAILED(find_extent_reader(internal_key_comparator,
                               extent_id,
                               options.read_tier == common::kBlockCacheTier /*no_io=*/,
                               false /*skip_filters*/,
                               level,
                               true /*prefetch_index_and_filter_in_cache*/,
                               &handle))) {
    SE_LOG(WARN, "fail to find reader", K(ret), K(extent_id));
  } else if (IS_NULL(reader = get_extent_reader_from_handle(handle))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "the reader must not be nullptr", K(ret), K(extent_id));
  } else if (FAILED(reader->get(key, get_context))) {
    SE_LOG(WARN, "fail to get key", K(ret), K(extent_id));
  } else {
    STRESS_CHECK_APPEND(GET_FROM_EXTENT, Status::kOk);
  }

  if (IS_NOTNULL(handle)) {
    release_handle(handle);
    handle = nullptr;
  }

  return ret;
}

int TableCache::find_extent_reader(const InternalKeyComparator &internal_key_comparator,
                                  const ExtentId &extent_id,
                                  const bool no_io,
                                  const bool skip_filters,
                                  const int32_t level,
                                  const bool prefetch_index_and_filter_in_cache,
                                  Cache::Handle **handle)
{
  int ret = Status::kOk;
  ExtentReader *extent_reader = nullptr;
  uint64_t id = extent_id.id();
  Slice key = GetSliceForFileNumber(&id); 

  if (IS_NULL(*handle = cache_->Lookup(key))) {
    // cache miss, create it
    if (no_io) {
      ret = Status::kIncomplete;
      SE_LOG(WARN, "can't find table reader from cache", K(extent_id));
    } else if (FAILED(create_extent_reader(internal_key_comparator,
                                           extent_id,
                                           skip_filters,
                                           level,
                                           prefetch_index_and_filter_in_cache,
                                           extent_reader))) {
      SE_LOG(WARN, "fail to get table reader", K(ret));
    } else if (FAILED(cache_->Insert(key,
                                     extent_reader,
                                     extent_reader->get_usable_size(),
                                     &DeleteEntry<ExtentReader>,
                                     handle).code())) {
    
    } else{
      //TODO(Zhao Dongsheng): The size of ExtentReader including index block
      // has been calculated in get_usable_size.
      //extent_reader->set_mod_id(ModId::kTableCache);
    }
  }

  return ret;
}

ExtentReader *TableCache::get_extent_reader_from_handle(Cache::Handle *handle)
{
  return reinterpret_cast<ExtentReader *>(cache_->Value(handle));
}

void TableCache::release_handle(Cache::Handle* handle)
{
  cache_->Release(handle);
}

void TableCache::evict(Cache* cache, uint64_t file_number)
{
  cache->Erase(GetSliceForFileNumber(&file_number));
}

int TableCache::create_extent_reader(const InternalKeyComparator &internal_key_comparator,
                                     const ExtentId &extent_id,
                                     const bool skip_filters,
                                     const int32_t level,
                                     const bool prefetch_index_and_filter_in_cache,
                                     ExtentReader *&extent_reader)
{
  int ret = Status::kOk;
  //TODO(Zhao Dongsheng): It's ugly to get block cache here.
  ExtentBasedTableFactory *tmp_factory = reinterpret_cast<ExtentBasedTableFactory *>(ioptions_.table_factory);
  Cache *block_cache = tmp_factory->table_options().block_cache.get();
  ExtentReaderArgs args(extent_id, &internal_key_comparator, block_cache);

  if (IS_NULL(extent_reader = MOD_NEW_OBJECT(ModId::kTableCache, ExtentReader))) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "fail to allocate memory for ExtentReader", K(ret));
  } else if (FAILED(extent_reader->init(args))) {
    SE_LOG(WARN, "fail to init extent reader", K(ret));
  } else {
    QUERY_COUNT(CountPoint::NUMBER_FILE_OPENS);
  }

  // resource clean
  if (FAILED(ret)) {
    if (IS_NOTNULL(extent_reader)) {
      MOD_DELETE_OBJECT(ExtentReader , extent_reader);
    }
  }

  return ret;
}
}  // namespace db
}  // namespace smartengine
