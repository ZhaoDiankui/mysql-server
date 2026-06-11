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
//
// Thread-safe (provides internal synchronization)

#pragma once

#include "cache/cache.h"
#include "db/dbformat.h"
#include "options/cf_options.h"

namespace smartengine
{
namespace table
{
class ExtentReader;
class GetContext;
class InternalIterator;
class TableReader;
}

namespace db
{
class TableCache
{
public:
  TableCache(const common::ImmutableCFOptions& ioptions, cache::Cache* cache);
  ~TableCache();

  // Return an iterator for the specified file number (the corresponding
  // file length must be exactly "file_size" bytes).  If "tableptr" is
  // non-nullptr, also sets "*tableptr" to point to the Table object
  // underlying the returned iterator, or nullptr if no Table object underlies
  // the returned iterator.  The returned "*tableptr" object is owned by
  // the cache and should not be deleted, and is valid for as long as the
  // returned iterator is live.
  // @param skip_filters Disables loading/accessing the filter block
  // @param level The level this table is at, -1 for "not set / don't know"
  table::InternalIterator* create_iterator(const common::ReadOptions& options,
                                           const InternalKeyComparator& internal_comparator,
                                           int32_t level,
                                           const storage::ExtentId &extent_id,
                                           table::ExtentReader *&extent_reader);

  // If a seek to internal key "k" in specified file finds an entry,
  // call (*handle_result)(arg, found_key, found_value) repeatedly until
  // it returns false.
  // @param get_context State for get operation.
  // If an error occurs, returns non-ok status.
  // @param skip_filters Disables loading/accessing the filter block
  // @param level The level this table is at, -1 for "not set / don't know"
  int get(const common::ReadOptions& options,
          const InternalKeyComparator& internal_comparator,
          const int32_t level,
          const storage::ExtentId &extent_id,
          const common::Slice& key,
          table::GetContext* get_context);

  int find_extent_reader(const InternalKeyComparator &internal_key_comparator,
                        const storage::ExtentId &extent_id,
                        const bool no_io,
                        const bool skip_filters,
                        const int32_t level,
                        const bool prefetch_index_and_filter_in_cache,
                        cache::Cache::Handle **handle);

  // Get ExtentReader from a cache handle.
  table::ExtentReader *get_extent_reader_from_handle(cache::Cache::Handle *handle);

  // Release the handle from a cache
  void release_handle(cache::Cache::Handle* handle);

  // Evict any entry for the specified file number
  static void evict(cache::Cache* cache, uint64_t file_number);

private:
  // Build a extent reader
  int create_extent_reader(const InternalKeyComparator &internal_key_comparator,
                           const storage::ExtentId &extent_id,
                           const bool skip_filters,
                           const int32_t level,
                           const bool prefetch_index_and_filter_in_cache,
                           table::ExtentReader *&extent_reader);
private:
  const common::ImmutableCFOptions& ioptions_;
  cache::Cache* const cache_;
};

} // namespace db
} // namespace smartengine
