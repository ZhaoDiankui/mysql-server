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
#ifndef SMARTENGINE_REUSE_BLOCK_MERGE_ITERATOR_H_
#define SMARTENGINE_REUSE_BLOCK_MERGE_ITERATOR_H_

#include <stdint.h>
#include <bitset>
#include "compact/range_iterator.h"
#include "logger/log_module.h"
#include "storage/multi_version_extent_meta_layer.h"
#include "storage/storage_meta_struct.h"
#include "table/iter_heap.h"
#include "table/iterator_wrapper.h"
#include "util/heap.h"
#include "memory/page_arena.h"

namespace smartengine {
namespace storage {

class MetaDataIterator : public RangeAdaptorIterator<table::InternalIterator> {
 public:
  // MetaDataIterator() : RangeAdaptorIterator<Iterator>() {}
  explicit MetaDataIterator(const storage::MetaType &type, table::InternalIterator *iter,
                            const storage::Range &range,
                            const util::Comparator &comparator,
                            const int64_t delete_percent = 0)
      : RangeAdaptorIterator<table::InternalIterator>(type, iter),
        meta_comparator_(comparator),
        seek_bound_(range),
        level_(type.level_),
        delete_percent_(delete_percent),
        need_skip_(false),
        arena_(memory::CharArena::DEFAULT_PAGE_SIZE, memory::ModId::kCompaction) {
    int64_t pos = 0;
    seek_key_.DecodeFrom(seek_bound_.start_key_);
    last_userkey_.clear();
  }
  virtual ~MetaDataIterator() override {}

 public:
  virtual void seek_to_first() override {
    assert(iter_);
    iter_->Seek(seek_bound_.start_key_);
    update();
  }
  virtual void next() override {
    assert(iter_);
    if (start_) {
      start_ = false;
    } else {
      do {
        iter_->Next();
        update();
      } while (need_skip_ && valid_);
    }
  }
 protected:
  virtual int extract_range(const common::Slice &key_in, const common::Slice &value_in) override;

  const util::Comparator &meta_comparator_;
  storage::Range seek_bound_;
  db::InternalKey seek_key_;
  int64_t level_;
  int64_t delete_percent_;
  bool need_skip_;
  common::Slice last_userkey_;
  memory::ArenaAllocator arena_;
};

//class MetaDataSingleIterator : public RangeAdaptorIterator<db::Iterator> {
class MetaDataSingleIterator : public RangeAdaptorIterator<table::InternalIterator> {
 public:
  explicit MetaDataSingleIterator(const storage::MetaType &type, table::InternalIterator *iter)
      : RangeAdaptorIterator<table::InternalIterator>(type, iter),
        level_(type.level_),
        arena_(memory::CharArena::DEFAULT_PAGE_SIZE, memory::ModId::kCompaction) {
  }
  virtual ~MetaDataSingleIterator() override {}

 public:
  virtual void seek_to_first() override {
    assert(iter_);
    iter_->SeekToFirst();
    valid_ = iter_->Valid();
    if (valid_) {
      status_ = extract_range(iter_->key(), iter_->value());
    }
  }
  virtual void next() override {
    assert(iter_);
    iter_->Next();
    valid_ = iter_->Valid();
    if (valid_) {
      status_ = extract_range(iter_->key(), iter_->value());
    }
  }
 protected:
  virtual int extract_range(const common::Slice &key_in,
                            const common::Slice &value_in) override {
    int ret = 0;
    ExtentMeta *extent_meta = nullptr;
    int64_t pos = 0;

    if (nullptr == (extent_meta = reinterpret_cast<ExtentMeta *>(const_cast<char *>(key_in.data())))) {
      ret = common::Status::kErrorUnexpected;
      COMPACTION_LOG(WARN, "unexpected error, extent meta must not nullptr", K(ret));
    } else {
      extent_meta_ = extent_meta;
      meta_descriptor_.range_.start_key_ = extent_meta->smallest_key_.Encode().deep_copy(arena_);
      meta_descriptor_.range_.end_key_ = extent_meta->largest_key_.Encode().deep_copy(arena_);
      meta_descriptor_.block_position_.first = extent_meta->extent_id_.id();
      meta_descriptor_.layer_position_ = (reinterpret_cast<ExtentLayerIterator *>(iter_))->get_layer_position();
      meta_descriptor_.extent_id_ = extent_meta->extent_id_;

      //
      meta_descriptor_.type_.sequence_ = 0;
      meta_descriptor_.key_ = key_in;
      meta_descriptor_.value_ = value_in;
    }
    return ret;
  }

  int64_t level_;
  memory::ArenaAllocator arena_;
};

template <typename IteratorWrapper>
class MinHeapComparator {
 public:
  MinHeapComparator(const util::Comparator *comparator)
      : comparator_(comparator) {}

  bool operator()(IteratorWrapper *a, IteratorWrapper *b) const {
    int cmp = comparator_->Compare(a->user_key(), b->user_key());
    if (0 == cmp)
      return !a->middle() && b->middle();
    else
      return cmp > 0;
  }

 private:
  const util::Comparator *comparator_;
};

class ReuseBlockMergeIterator {
 public:
  static const int64_t RESERVE_CHILD_NUM = 4;
  static const int64_t RESERVE_DESC_NUM = 16;
  static const int64_t MAX_CHILD_NUM = 16;

  typedef util::BinaryHeap<RangeIterator *, MinHeapComparator<RangeIterator> >
      MergerMinIterHeap;

 public:
  ReuseBlockMergeIterator(memory::ArenaAllocator &allocator,
                          const util::Comparator &comparator);
  virtual ~ReuseBlockMergeIterator();

  // set_children will reset all iterators in merging context.
  // make sure we are not in middle of iterate loop;
  int set_children(RangeIterator **children, int64_t child_num);
  RangeIterator **get_children(int64_t &child_num) {
    child_num = child_num_;
    return children_;
  }

  bool valid() const;
  void seek_to_first();
  void seek(const common::Slice &target);
  void next();
  common::Status status() const;

  const MetaDescriptorList &get_output() const { return output_; }

 private:
  void find_next_closed_range();
  void add_output(RangeIterator &iter);
  void reset_context();
  bool all_iterator_closed() const;

 private:
  memory::ArenaAllocator &allocator_;
  const util::Comparator &comparator_;
  MergerMinIterHeap min_heap_;
  RangeIterator *children_[MAX_CHILD_NUM];
  int64_t child_num_;

  MetaDescriptorList output_;
  std::bitset<MAX_CHILD_NUM> iterator_states_;
  bool inited_;
  int status_;

  ReuseBlockMergeIterator(const ReuseBlockMergeIterator &) = delete;
  ReuseBlockMergeIterator &operator=(const ReuseBlockMergeIterator &) = delete;
};


} // storage
} // smartengine

#endif  // SMARTENGINE_REUSE_BLOCK_MERGE_ITERATOR_H_
