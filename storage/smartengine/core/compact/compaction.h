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

#ifndef SMARTENGINE_STORAGE_COMPACTION_H_
#define SMARTENGINE_STORAGE_COMPACTION_H_

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif
#include "compact/compaction_stats.h"
#include "compact/range_iterator.h"
#include "compact/task_type.h"
#include "env/env.h"
#include "memory/page_arena.h"
#include "options/options.h"
#include "storage/change_info.h"
#include "util/aligned_buffer.h"

namespace smartengine
{
namespace db
{
class InternalKeyComparator;
} // namespace db

namespace util
{
struct EnvOptions;
class Env;
class Comparator;
} // namespace util
namespace common
{
struct ImmutableCFOptions;
struct MutableCFOptions;
} // namespace common
namespace table
{
class BlockIter;
class ExtentWriter;
} // namespace table
namespace storage
{
class NewCompactionIterator;

struct CompactionContext
{
  const std::atomic<bool> *shutting_down_;
  const std::atomic<bool> *bg_stopped_;
  const std::atomic<int64_t> *cancel_type_;
  const common::ImmutableCFOptions *cf_options_;
  const common::MutableCFOptions *mutable_cf_options_;
  const util::EnvOptions *env_options_;
  // user Comparator in data block level;
  const util::Comparator *data_comparator_;
  const db::InternalKeyComparator *internal_comparator_;
  int64_t table_space_id_;
  common::SequenceNumber earliest_write_conflict_snapshot_;
  std::vector<common::SequenceNumber> existing_snapshots_;
  // maybe 0, 1, or 2, default is 1
  int32_t output_level_;
  db::TaskType task_type_;
  // Intra Level-0 need to force all the reused and newly created extents
  // to have the same layer sequence.
  // Selected layer sequence should be different from the old layer sequences.
  // We use smallest (Extent.type_.sequence_-1) as the selected layer sequence.
  // Default: -1, means have not been set.
  int64_t force_layer_sequence_;
  bool enable_thread_tracking_;
  bool need_check_snapshot_;

  CompactionContext()
      : shutting_down_(nullptr),
        bg_stopped_(nullptr),
        cancel_type_(nullptr),
        cf_options_(nullptr),
        mutable_cf_options_(nullptr),
        env_options_(nullptr),
        data_comparator_(nullptr),
        internal_comparator_(nullptr),
        table_space_id_(-1),
        earliest_write_conflict_snapshot_(0),
        output_level_(1),
        task_type_(db::TaskType::INVALID_TYPE_TASK),
        force_layer_sequence_(-1),
        enable_thread_tracking_(false),
        need_check_snapshot_(true)
  {
    existing_snapshots_.clear();
  }

  bool valid() const {
    return nullptr != shutting_down_ && nullptr != bg_stopped_ &&
           nullptr != cf_options_ &&
           nullptr != mutable_cf_options_ && nullptr != env_options_ &&
           nullptr != data_comparator_ && nullptr != internal_comparator_ &&
           table_space_id_ >= 0;
  }
};

// TODO(Zhao Dongsheng) : The structure ColumnFamilyDesc is useless,
// because the column_family_id_ is exist in table_schema_.
struct ColumnFamilyDesc {
  int32_t column_family_id_;
  schema::TableSchema table_schema_;

  ColumnFamilyDesc() : column_family_id_(0),
                       table_schema_()
  {}

  ColumnFamilyDesc(int32_t index_id,
                   const schema::TableSchema &table_schema)
      : column_family_id_(index_id),
        table_schema_(table_schema)
  {}
};

class Compaction {
 public:
  // Compaction output
  struct Statstics {
    CompactRecordStats record_stats_;
    CompactPerfStats perf_stats_;
  };

  enum Level {
    L0 = 0,
    L1 = 1,
    L2 = 2,
  };

 public:
  virtual ~Compaction(){}
  // input extent meta data iterator;
  virtual int add_merge_batch(
      const MetaDescriptorList &extents,
      const size_t start, const size_t end) = 0;
  virtual int run() = 0;
  virtual int cleanup() = 0;

  virtual const Statstics &get_stats() const = 0;
  virtual storage::ChangeInfo &get_change_info() = 0;
  virtual const int64_t *get_input_extent() const = 0;
};

// compaction history information schema
struct CompactionJobStatsInfo {
  uint32_t subtable_id_;
  int64_t sequence_;
  int type_;
  Compaction::Statstics stats_;
};

class GeneralCompaction : public Compaction {
  static const int64_t DEFAULT_ROW_LENGTH = 8 * 1024;  // 8kb
  static const int64_t RESERVE_READER_NUM = 64;
  static const int64_t RESERVE_MERGE_WAY_SIZE = 16;
 public:
  GeneralCompaction(const CompactionContext &context,
                    const ColumnFamilyDesc &cf,
                    memory::ArenaAllocator &arena);
  virtual ~GeneralCompaction() override;

  virtual int run() override;
  virtual int cleanup() override;

  // input extent meta data iterator;
  virtual int add_merge_batch(
      const MetaDescriptorList &extents,
      const size_t start, const size_t end) override;

  virtual const Statstics &get_stats() const override { return stats_; }
  virtual storage::ChangeInfo &get_change_info() override {
    return change_info_;
  }

  virtual const int64_t *get_input_extent() const override {
    return input_extents_;
  }

  size_t get_extent_size() const { return merge_extents_.size(); }
  // set level2's largest key
  void set_level2_largest_key(const common::Slice *l2_largest_key) {
    l2_largest_key_ = l2_largest_key;
  }
  // set delete percent
  void set_delete_percent(const int64_t delete_percent) {
    delete_percent_ = delete_percent;
  }
  int down_level_extent(const MetaDescriptor &extent);
  int copy_data_block(const MetaDescriptor &data_block
                      /*const common::SeSchema *schema*/);
  int create_extent_index_iterator(const MetaDescriptor &extent,
                                   int64_t &iterator_index,
                                   DataBlockIterator *&iterator,
                                   ExtSEIterator::ReaderRep &rep);
  int destroy_extent_index_iterator(const int64_t iterator_index);
  int delete_extent_meta(const MetaDescriptor &extent);
  bool check_do_reuse(const MetaDescriptor &meta) const;
 protected:
  friend class ExtSEIterator;
  struct PrefetchedExtent
  {
    table::ExtentReader *reader_;
    util::AIOHandle aio_handle_;

    PrefetchedExtent() : reader_(nullptr), aio_handle_() {}
    ~PrefetchedExtent() { aio_handle_.reset(); }
  };

  int open_extent();
  int close_extent(db::MiniTables *flush_tables = nullptr);

  void start_record_compaction_stats();
  void stop_record_compaction_stats();

  int build_multiple_seiterators(const int64_t batch_index,
      const storage::BlockPosition &batch,
      MultipleSEIterator *&merge_iterator);
  int build_compactor(NewCompactionIterator *&compactor,
      MultipleSEIterator *merge_iterator);

  int merge_extents(MultipleSEIterator *&merge_iterator,
      db::MiniTables *flush_tables = nullptr);

  int prefetch_extent(int64_t extent_id, PrefetchedExtent *&prefetched_extent);

  int create_data_block_iterator(table::ExtentReader *extent_reader,
                                 table::BlockDataHandle<table::RowBlock> &data_block_handle,
                                 table::RowBlockIterator *&block_iterator);

  int destroy_data_block_iterator(table::RowBlockIterator *block_iterator);

  PrefetchedExtent *get_prefetched_extent(int64_t extent_id) const;

  virtual void clear_current_readers();

  //TODO(Zhao Dongsheng): the function name is confused.
  virtual void clear_current_writers();

  void record_compaction_iterator_stats(
      const NewCompactionIterator &compactor,
      CompactRecordStats &stats);

 protected:

  using PrefetchExtentMap = std::unordered_map<int64_t, PrefetchedExtent *,
  std::hash<int64_t>, std::equal_to<int64_t>,
  memory::stl_adapt_allocator<std::pair<const int64_t,
  PrefetchedExtent *>, memory::ModId::kCompaction>>;

  //TODO(Zhao Dongsheng): dummy dict?
  std::string compression_dict_;
  // options for create builder and reader;
  CompactionContext context_;
  ColumnFamilyDesc cf_desc_;

  // all extents need to merge in one compaciton task.
  MetaDescriptorList merge_extents_;
  // [start, end) sub task in %merge_extents_;
  BlockPositionList merge_batch_indexes_;
  PrefetchExtentMap prefetched_extents_;

  // compaction writer,
  bool write_extent_opened_;
  table::ExtentWriter *extent_writer_;
  util::WritableBuffer block_buffer_;
  db::MiniTables mini_tables_;
  storage::ChangeInfo change_info_;

  // Compaction result, written meta, statistics;
  Statstics stats_;

  // information schema compaction input extents
  int64_t input_extents_[3]; // todo to check max_level
  // for minor
  const common::Slice *l2_largest_key_;
  // for major
  int64_t delete_percent_;
  ExtSEIterator *se_iterators_;
  memory::ArenaAllocator arena_;
  memory::WrapAllocator wrap_alloc_;
  memory::stl_adapt_allocator<ExtSEIterator::ReaderRep> stl_alloc_;
  using ReadRepList = std::vector<ExtSEIterator::ReaderRep, memory::stl_adapt_allocator<ExtSEIterator::ReaderRep>>;
  ReadRepList reader_reps_;
};

}  // namespace storage
}  // namespace smartengine

#endif
