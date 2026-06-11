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

#pragma  once

#include "storage/storage_meta_struct.h"
#include "table/sstable_scan_struct.h"

namespace smartengine
{
namespace storage
{
struct ExtentMeta;
class ReadableExtent;
} // namespace storage

namespace table
{
class GetContext;
class RowBlockIterator;

struct ExtentReaderArgs
{
  storage::ExtentId extent_id_;
  const db::InternalKeyComparator *internal_key_comparator_;
  cache::Cache *block_cache_;

  ExtentReaderArgs(const storage::ExtentId &extent_id,
                   const db::InternalKeyComparator *internal_key_comparator,
                   cache::Cache *block_cache)
      : extent_id_(extent_id),
        internal_key_comparator_(internal_key_comparator),
        block_cache_(block_cache)
  {}

  bool is_valid() const { return IS_NOTNULL(internal_key_comparator_); }
  DECLARE_TO_STRING()
};

class ExtentReader
{
public:
  ExtentReader();
  ExtentReader(const ExtentReader &) = delete;
  ExtentReader(const ExtentReader &&) = delete;
  ExtentReader &operator=(const ExtentReader &) = delete;
  ExtentReader &operator=(const ExtentReader &&) = delete;
  ~ExtentReader();

  int init(const ExtentReaderArgs &args);
  void destroy();
  int get(const common::Slice &key, GetContext *get_context);
  InternalIterator *NewIterator(const common::ReadOptions &read_options,
                                memory::SimpleAllocator *allocaor,
                                bool skip_filters,
                                const uint64_t scan_add_blocks_limit);
  int prefetch_index_block(BlockDataHandle<RowBlock> &handle);
  int prefetch_data_block(BlockDataHandle<RowBlock> &handle);
  int do_io_prefetch(const int64_t offset, const int64_t size, util::AIOHandle *aio_handle);
  int get_data_block(const BlockHandle &handle, util::AIOHandle *aio_handle, char *buf, common::Slice &block);
  int setup_index_block_iterator(RowBlockIterator *index_block_iterator);
  int setup_data_block_iterator(const bool use_cache,
                                BlockDataHandle<RowBlock> &data_block_handle,
                                RowBlockIterator *data_block_iterator);
  // setup data block iterator for scan.
  int setup_data_block_iterator(const int64_t scan_add_blocks_limit,
                                int64_t &added_blocks,
                                BlockDataHandle<RowBlock> &data_block_handle,
                                RowBlockIterator *data_block_iterator);

  int check_block_in_cache(const BlockHandle &handle, bool &in_cache);
  int approximate_key_offet(const common::Slice &key, int64_t &offset);
  int64_t get_data_size() const { return extent_meta_->data_size_; }
  int64_t get_row_count() const { return extent_meta_->num_entries_; }
  int64_t get_usable_size() const;

private:
  int check_in_bloom_filter(const common::Slice &user_key, const BlockInfo &block_info, bool &may_exist);
  int inner_get(const common::Slice &key,
                const BlockInfo &block_info,
                GetContext *get_context,
                bool &found);
  int get_data_block_through_cache(BlockDataHandle<RowBlock> &data_block_handle);
  int get_data_block_bypass_cache(BlockDataHandle<RowBlock> &data_block_handle);
  int read_index_block(const BlockHandle &handle, RowBlock *&row_block);
  int read_data_block(const BlockInfo &block_info, util::AIOHandle *aio_handle, RowBlock *&row_block);
  int read_block(const BlockHandle &handle, util::AIOHandle *aio_handle, char *buf, common::Slice &block);
  int read_and_uncompress_block(const BlockHandle &handle, util::AIOHandle *aio_handle, char *buf, common::Slice &block);
  int convert_to_row_block(const common::Slice &column_block,
                           const BlockInfo &block_info,
                           common::Slice &row_block);

private:
  bool is_inited_;
  const db::InternalKeyComparator *internal_key_comparator_;
  storage::IOExtent *extent_;
  storage::ExtentMeta *extent_meta_;
  RowBlock *index_block_;
  cache::Cache *block_cache_;
  cache::CacheEntryKey cache_entry_key_;
};

} // namespace table
} // namespace smartengine