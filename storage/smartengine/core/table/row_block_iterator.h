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

#include "db/dbformat.h"
#include "db/pinned_iterators_manager.h"
#include "table/internal_iterator.h"
#include "table/row_block.h"

namespace smartengine
{
namespace table
{

class RowBlockIterator : public InternalIterator
{
 public:
  RowBlockIterator()
      : comparator_(nullptr),
        current_(0),
        restart_index_(0),
        status_(common::Status::OK()),
        key_pinned_(false),
        last_(0),
        is_index_block_(false),
        prev_entries_idx_(-1)
    {}

  int setup(const util::Comparator *comparator, const RowBlock *block, const bool is_index_block);

  void reset();

  void SetStatus(common::Status s) { status_ = s; }

  virtual bool Valid() const override 
  { 
    return is_index_block_ ? (current_ <= last_ && current_ < restarts_offset_) : current_ < last_;
  }
  virtual common::Status status() const override { return status_; }
  virtual common::Slice key() const override {
    assert(Valid());
    return key_.GetInternalKey();
  }
  virtual common::Slice value() const override {
    assert(Valid());
    return value_;
  }

  virtual void Next() override;

  virtual void Prev() override;

  virtual void Seek(const common::Slice& target) override;

  virtual void SeekForPrev(const common::Slice& target) override;

  virtual void SeekToFirst() override;

  virtual void SeekToLast() override;


  virtual bool IsKeyPinned() const override { return key_pinned_; }

  virtual bool IsValuePinned() const override { return true; }

  uint32_t ValueOffset() const {
    return static_cast<uint32_t>(value_.data() - block_->data_);
  }

#ifndef NDEBUG
  ~RowBlockIterator() override {
    // Assert that the RowBlockIterator is never deleted while Pinning is Enabled.
    assert(!pinned_iters_mgr_ ||
           (pinned_iters_mgr_ && !pinned_iters_mgr_->PinningEnabled()));
  }
  virtual void SetPinnedItersMgr(
      db::PinnedIteratorsManager* pinned_iters_mgr) override {
    pinned_iters_mgr_ = pinned_iters_mgr;
  }
  db::PinnedIteratorsManager* pinned_iters_mgr_ = nullptr;

  size_t TEST_CurrentEntrySize() { return NextEntryOffset() - current_; }
#endif

private:
  void seek_end_key();

private:
  const util::Comparator* comparator_;
  const RowBlock *block_;
  // Offset of restart array (list of fixed32)
  uint32_t restarts_offset_;
  // Count of uint32_t entries in restart array
  uint32_t restarts_count_;
  // current_ is offset in data_ of current entry.  >= restarts_ if !Valid
  uint32_t current_;
  // index of restart block which current_ falls in. current_ is between
  // restart_array[restart_index_] and restart_array[restart_index + 1].
  uint32_t restart_index_;
  // current key with offset as current_
  db::IterKey key_;
  common::Slice value_;
  common::Status status_;
  bool key_pinned_;
  // last_ is offset in data_ of last key.
  // If the end key push is set, the last_ is offset of
  // first key which is greater than or equal to end_ikey_(see the
  // semantics of 'end_ikey_' at its declaration which located in
  // InternalIterator). If the end key push is not set, the last_
  // is offset of restart array. 
  uint32_t last_;
  // Used by Valid check. The key of index block entry is
  // the largest key of the corresponding data block, so
  // the data block corresponding to the last index entry may
  // contain the valid result.
  // For example, if the range condition is "a > 10 and a <=20",
  // the key of last index entry is 20 which corresponding data
  // block contain range [15, 20].
  bool is_index_block_;

  struct CachedPrevEntry {
    explicit CachedPrevEntry(uint32_t _offset, const char* _key_ptr,
                             size_t _key_offset, size_t _key_size,
                             common::Slice _value)
        : offset(_offset),
          key_ptr(_key_ptr),
          key_offset(_key_offset),
          key_size(_key_size),
          value(_value) {}

    // offset of entry in block
    uint32_t offset;
    // Pointer to key data in block (nullptr if key is delta-encoded)
    const char* key_ptr;
    // offset of key in prev_entries_keys_buff_ (0 if key_ptr is not nullptr)
    size_t key_offset;
    // size of key
    size_t key_size;
    // value slice pointing to data in block
    common::Slice value;
  };
  std::string prev_entries_keys_buff_;
  std::vector<CachedPrevEntry> prev_entries_;
  int32_t prev_entries_idx_ = -1;

  inline int Compare(const common::Slice& a, const common::Slice& b) const {
    return comparator_->Compare(a, b);
  }

  // Return the offset in data_ just past the end of the current entry.
  inline uint32_t NextEntryOffset() const {
    // NOTE: We don't support blocks bigger than 2GB
    return static_cast<uint32_t>((value_.data() + value_.size()) - block_->data_);
  }

  inline uint32_t next_entry_offset(const common::Slice& value) const
  {
    return static_cast<uint32_t>((value.data() + value.size()) - block_->data_);
  }

  uint32_t GetRestartPoint(uint32_t index) {
    assert(index < restarts_count_);
    return util::DecodeFixed32(block_->data_ + restarts_offset_ + index * RowBlock::RESTARTS_ELEMENT_SIZE);
  }

  void SeekToRestartPoint(uint32_t index) {
    key_.Clear();
    restart_index_ = index;
    // current_ will be fixed by ParseNextKey();

    // ParseNextKey() starts at the end of value_, so set value_ accordingly
    uint32_t offset = GetRestartPoint(index);
    value_ = common::Slice(block_->data_ + offset, 0);
  }

  void CorruptionError();

  bool ParseNextKey();

  bool parse_next_key(uint32_t& current,
                      uint32_t& restart_index,
                      db::IterKey& key,
                      common::Slice& value,
                      bool& key_pinned);

  bool BinarySeek(const common::Slice& target, uint32_t left, uint32_t right,
                  uint32_t* index);
};

} // namespace table
} // namespace smartengine