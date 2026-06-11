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

#ifndef SMARTENGINE_RANGE_ITERATOR_ADAPTOR_H_
#define SMARTENGINE_RANGE_ITERATOR_ADAPTOR_H_

#include <stdint.h>
#include <utility>
#include "memory/stl_adapt_allocator.h"
#include "memory/mod_info.h"
#include "db/dbformat.h"
#include "table/extent_reader.h"
#include "table/internal_iterator.h"
#include "table/row_block.h"
#include "table/row_block_iterator.h"
#include "table/sstable_scan_struct.h"
#include "util/to_string.h"
#include "storage/storage_common.h"
#include "util/heap.h"

namespace smartengine
{
namespace table
{
  struct RowBlock;
} // namespace table

namespace storage
{

// can represents Extent or DataBlock
// if represents Extent, first and second are continuous extent_id
// if represents DataBlock, first is offset and second is size of data block;
typedef std::pair<int64_t, int64_t> BlockPosition;

struct CompactionContext;
struct ExtentMeta;
class GeneralCompaction;

struct Range
{
  common::Slice start_key_;
  common::Slice end_key_;

  Range deep_copy(memory::Allocator &allocator) const;
  Range deep_copy(memory::SimpleAllocator &allocator) const;
  DECLARE_TO_STRING()
};

struct MetaType
{
  enum StoreType {
    SSTable = 0,
    MemTable = 1,
  };
  enum DataType {
    Extent = 0,
    DataBlock = 1,
  };
  enum KeyType {
    InternalKey = 0,
    UserKey = 1,
  };

  int8_t store_type_;  // memtable or sstable
  int8_t data_type_;   // extent or data block
  int8_t key_type_;    // UserKey or InternalKey
  int8_t level_;       // sstable level
  int16_t way_;
  int64_t sequence_;

  MetaType();
  MetaType(int8_t st, int8_t dt, int8_t kt, int8_t lv, int16_t w, int64_t seq);
  DECLARE_TO_STRING()
};

//TODO(Zhao Dongsheng) : This structure is used to describe both extents and blocks, but its meaning is not very clear.
struct MetaDescriptor
{
  storage::MetaType type_;
  storage::Range range_;
  storage::BlockPosition block_position_;
  storage::LayerPosition layer_position_;
  ExtentId extent_id_;
  common::Slice key_;    // copy iterator's key
  common::Slice value_;  // copy iterator's value
  int64_t delete_percent_;
  table::BlockInfo block_info_;
  MetaDescriptor();
  MetaDescriptor deep_copy(memory::Allocator &allocator) const;
  MetaDescriptor deep_copy(memory::SimpleAllocator &allocator) const;
  DECLARE_TO_STRING()

  bool is_user_key() const { return type_.key_type_ == MetaType::UserKey; }
  const ExtentId get_extent_id() const { return extent_id_; }
  common::Slice get_user_key(const common::Slice &key) const {
    if (is_user_key()) {
      return key;
    } else {
      return db::ExtractUserKey(key);
    }
  }
  common::Slice get_start_user_key() const {
    return get_user_key(range_.start_key_);
  }
  common::Slice get_end_user_key() const {
    return get_user_key(range_.end_key_);
  }

};

using MetaDescriptorList = std::vector<MetaDescriptor,
  memory::stl_adapt_allocator<MetaDescriptor, memory::ModId::kCompaction>>;
using BlockPositionList = std::vector<BlockPosition,
  memory::stl_adapt_allocator<BlockPosition, memory::ModId::kCompaction>>;

class RangeIterator
{
 public:
  virtual ~RangeIterator() {}

  virtual bool middle() const = 0;
  virtual bool valid() const = 0;
  virtual common::Status status() const = 0;

  virtual common::Slice user_key() const = 0;
  virtual common::Slice key() const = 0;
  virtual common::Slice value() const = 0;

  virtual void seek(const common::Slice &lookup_key) = 0;
  virtual void seek_to_first() = 0;
  virtual void next() = 0;

  virtual const MetaDescriptor &get_meta_descriptor() const = 0;
};

/**
 * RangeAdaptorIterator takes a range(start_key,end_key) iterator
 * and iterate start_key, end_key sequentially.
 * just forward, Do not iterate backward direction
 */
template <typename IterType>
class RangeAdaptorIterator : public RangeIterator
{
 public:
  RangeAdaptorIterator()
      : valid_(false), start_(false), status_(0), iter_(nullptr), extent_meta_(nullptr) {}
  explicit RangeAdaptorIterator(const MetaType &meta_type, IterType *iter)
      : valid_(false), start_(false), status_(0), iter_(iter), extent_meta_(nullptr) {
    meta_descriptor_.type_ = meta_type;
    update();
  }

  virtual ~RangeAdaptorIterator() override {}

  IterType *get_inner_iterator() { return iter_; }

  const storage::MetaDescriptor &get_meta_descriptor() const override {
    assert(valid_);
    return meta_descriptor_;
  }

  const storage::ExtentMeta* get_extent_meta() const {
    assert(valid_);
    return extent_meta_;
  }

  // we are in the middle of the range;
  virtual bool middle() const override { return start_; }
  // still has more data if is valid.
  virtual bool valid() const override { return valid_; }

  virtual common::Slice user_key() const override {
    assert(valid_);
    return db::ExtractUserKey(key());
  }

  virtual common::Slice key() const override {
    assert(valid_);
    return start_ ? meta_descriptor_.range_.start_key_
                  : meta_descriptor_.range_.end_key_;
  }

  virtual common::Slice value() const override {
    assert(valid_);
    assert(iter_);
    return iter_->value();
  }

  virtual common::Status status() const override {
    assert(iter_);
    if (common::Status::kOk != status_) return common::Status(status_);
    return iter_->status();
  }

  virtual void next() override {
    assert(iter_);
    if (start_) {
      start_ = false;
    } else {
      iter_->Next();
      update();
    }
  }

  virtual void seek(const common::Slice &lookup_key) override {
    assert(iter_);
    iter_->Seek(lookup_key);
    update();
  }

  virtual void seek_to_first() override {
    assert(iter_);
    iter_->SeekToFirst();
    update();
  }

 protected:
  // update shoudle setup all data we need.
  virtual void update() {
    if (nullptr != iter_) {
      valid_ = iter_->Valid();
      if (valid_) {
        start_ = true;
        status_ = extract_range(iter_->key(), iter_->value());
      }
    }
  }

  virtual int extract_range(const common::Slice &k, const common::Slice &v) {
    UNUSED(k);
    UNUSED(v);
    return 0;
  }

 protected:
  bool valid_;
  bool start_;  // start_ == true if we'are in the middle of range;
  int status_;
  IterType *iter_;
  storage::MetaDescriptor meta_descriptor_;
  storage::ExtentMeta* extent_meta_;
};

class DataBlockIterator
{
 public:
  explicit DataBlockIterator(const MetaType type, table::RowBlockIterator *index_iterator);
  virtual ~DataBlockIterator();

  void seek_to_first();
  void next();
  int update(const common::Slice &start, const common::Slice &end);
  bool valid() const { return IS_NOTNULL(index_iterator_) && index_iterator_->Valid(); }
  const MetaDescriptor &get_meta_descriptor() const { return meta_descriptor_; }

 private:
  MetaDescriptor meta_descriptor_;
  table::RowBlockIterator *index_iterator_;
};

class SEIterator {
 public:
  enum IterLevel { kExtentLevel, kBlockLevel, kKVLevel, kDataEnd };
  SEIterator();
  virtual ~SEIterator();
  virtual void seek_to_first() = 0;
  // through compare other key to check reuse
  // if kv level, get (key,value)
  // if not kv level, do reuse and return output_level=block/extent
  virtual int get_current_row(const common::Slice &other_key,
                              const common::Slice &last_key,
                              const bool has_last_key,
                              IterLevel &output_level) = 0;
  // deal with equal condition
  // if block/extent level, just open util it turn to kv level
  // if kv level, get (key, value)
  virtual int get_special_curent_row(IterLevel &out_level) = 0;

  // if block/extent level, do reuse while it turn to kv level
  // if kv level, get (key, value)
  virtual int get_single_row(const common::Slice &last_key,
                             const bool has_last_key,
                             IterLevel &out_level) = 0;
  virtual int next() = 0;
  virtual void reset() = 0;
  virtual bool valid() const = 0;
  virtual common::Slice get_key() const = 0;
  virtual common::Slice get_value() const = 0;
  virtual bool has_data() const = 0;
  // internal start key
  inline const common::Slice &get_startkey() const {
    assert(kDataEnd != iter_level_);
    return startkey_;
  }
  // internal end key
  inline const common::Slice &get_endkey() const {
    assert(kDataEnd != iter_level_);
    return endkey_;
  }
  // user key
  common::Slice get_start_ukey() const;
  common::Slice get_end_ukey() const;
  void set_compaction(GeneralCompaction *compaction) { compaction_ = compaction; }

public:
  GeneralCompaction *compaction_;
  IterLevel iter_level_;
  common::Slice startkey_;
  common::Slice endkey_;
};

class MemSEIterator : public SEIterator {
 public:
  MemSEIterator();
  virtual ~MemSEIterator() override;
  virtual void seek_to_first() override;
  // through compare other key to check reuse
  // if kv level, get (key,value)
  // if not kv level, do reuse and return output_level=block/extent
  virtual int get_current_row(const common::Slice &other_key,
                              const common::Slice &last_key,
                              const bool has_last_key,
                              IterLevel &output_level) override;
  // deal with equal condition
  // if block/extent level, just open util it turn to kv level
  // if kv level, get (key, value)
  virtual int get_special_curent_row(IterLevel &out_level) override;

  // if block/extent level, do reuse while it turn to kv level
  // if kv level, get (key, value)
  virtual int get_single_row(const common::Slice &last_key,
                             const bool has_last_key,
                             IterLevel &out_level) override;
  virtual int next() override;
  virtual void reset() override;
  virtual bool valid() const override {
    assert(mem_iter_);
    return mem_iter_->Valid();
  }
  virtual common::Slice get_key() const override {
    assert(mem_iter_);
    return mem_iter_->key();
  }
  virtual common::Slice get_value() const override {
    assert(mem_iter_);
    return mem_iter_->value();
  }
  virtual bool has_data() const override {
    return nullptr != mem_iter_;
  }
  void set_mem_iter(table::InternalIterator *mem_iter) {
    mem_iter_ = mem_iter;
  }
 private:
  int next_kv(IterLevel &output_level);
  table::InternalIterator* mem_iter_;
};

class ExtSEIterator : public SEIterator{
 public:
  static const int64_t RESERVE_READER_NUM = 64;
  struct ReaderRep {
    ReaderRep()
        : extent_id_(0),
          extent_reader_(nullptr),
          aio_handle_(nullptr),
          index_iterator_(nullptr),
          block_iter_(nullptr)
    {}
    int64_t extent_id_;
    table::ExtentReader *extent_reader_;
    util::AIOHandle *aio_handle_;
    table::RowBlockIterator *index_iterator_;
    DataBlockIterator *block_iter_;
  };

  ExtSEIterator(const util::Comparator *cmp,
             const util::Comparator *interal_cmp);
  virtual ~ExtSEIterator() override;
  virtual void seek_to_first() override;
  // through compare other key to check reuse
  // if kv level, get (key,value)
  // if not kv level, do reuse and return output_level=block/extent
  virtual int get_current_row(const common::Slice &other_key,
                              const common::Slice &last_key,
                              const bool has_last_key,
                              IterLevel &output_level) override;
  // deal with equal condition
  // if block/extent level, just open util it turn to kv level
  // if kv level, get (key, value)
  virtual int get_special_curent_row(IterLevel &out_level) override;

  // if block/extent level, do reuse while it turn to kv level
  // if kv level, get (key, value)
  virtual int get_single_row(const common::Slice &last_key,
                             const bool has_last_key,
                             IterLevel &out_level) override;
  virtual void reset() override;
  virtual bool valid() const override {
    return (kDataEnd != iter_level_);
  }
  virtual common::Slice get_key() const override {
    assert(current_block_iter_);
    return current_block_iter_->key();
  }
  virtual common::Slice get_value() const override {
    assert(current_block_iter_);
    return current_block_iter_->value();
  }
  virtual bool has_data() const override {
    return extent_list_.size() > 0;
  }
  virtual int next() override;
  // add extents' meta need to merge
  void add_extent(const storage::MetaDescriptor &extent) {
    extent_list_.push_back(extent);
  }
  void set_delete_percent(const int64_t delete_percent) {
    delete_percent_ = delete_percent;
  }
  // get extent/block meta
  const MetaDescriptor &get_meta_descriptor() const {
    assert(valid());
    if (kBlockLevel == iter_level_) {
      assert(current_iterator_);
      return current_iterator_->get_meta_descriptor();
    } else {
      assert(extent_index_ < extent_list_.size());
      return extent_list_[extent_index_];
    }
  }
  // return the reuse block/extent meta
  const MetaDescriptor &get_reuse_meta() const { return reuse_meta_; }
  IterLevel get_iter_level() const { return iter_level_; }
  size_t get_extent_index() const { return extent_index_; }
  int64_t get_extent_level() const {
    assert(extent_index_ < extent_list_.size());
    return extent_list_[extent_index_].type_.level_;
  }
 private:
  int next_extent();
  int next_block();
  int next_kv();
  int create_current_iterator();
  int check_reuse_meta(const common::Slice &last_key,
                       const bool has_last_key,
                       IterLevel &output_level);
  int create_block_iter(const MetaDescriptor &meta);
  void prefetch_next_extent();
  int get_mem_row(IterLevel &output_level);

 private:
  DataBlockIterator *current_iterator_;
  table::RowBlockIterator *current_block_iter_;
  table::BlockDataHandle<table::RowBlock> current_data_block_handle_;
  const util::Comparator *cmp_;
  const util::Comparator *internal_cmp_;
  int64_t iterator_index_;
  size_t extent_index_;
  bool reuse_;
  bool at_next_; // at next block or extent
  MetaDescriptorList extent_list_;
  ReaderRep cur_rep_;
  int64_t delete_percent_;
  MetaDescriptor reuse_meta_;
  memory::ArenaAllocator meta_descriptor_arena_;
};


class SEIteratorComparator {
 public:
  SEIteratorComparator(const util::Comparator* comparator)
      : comparator_(comparator) {}

  bool operator()(SEIterator * a, SEIterator* b) const {
    return comparator_->Compare(a->get_startkey(), b->get_startkey()) > 0;
  }

 private:
  const util::Comparator* comparator_;
};

class MultipleSEIterator {
public:
  MultipleSEIterator(const util::Comparator* user_cmp,
                     const util::Comparator* internal_cmp);
  ~MultipleSEIterator() {
  }
  typedef util::BinaryHeap<SEIterator *,
                           SEIteratorComparator>
      MergerSEIterHeap;
  void reset();
  int seek_to_first();
  int next();
  int get_single_row(const bool has_last_kv);
  int add_se_iterator(SEIterator *se_iterator);
  bool valid() const {
    return SEIterator::kDataEnd != output_level_;
  }
  common::Slice get_key() {
    assert(current_iterator_);
    return current_iterator_->get_key();
  }
  common::Slice get_value() {
    assert(current_iterator_);
    return current_iterator_->get_value();
  }

  // return the reuse block/extent meta
  const MetaDescriptor &get_reuse_meta() const {
    assert(current_iterator_);
    return static_cast<ExtSEIterator *>(current_iterator_)->get_reuse_meta();
  }

  void set_last_user_key(const common::Slice &last_user_key) {
    last_user_key_ = last_user_key;
  }

  SEIterator::IterLevel get_output_level() const {
    return output_level_;
  }
private:
  const util::Comparator *user_cmp_;
  const util::Comparator *internal_cmp_;
  MergerSEIterHeap se_heap_;
  SEIterator *current_iterator_;
  common::Slice last_user_key_;
  SEIterator::IterLevel output_level_;
  bool has_one_way_;
};
} /* meta */
} /* smartengine*/

#endif  // SMARTENGINE_RANGE_ITERATOR_ADAPTOR_H_
