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

#include "table/row_block_iterator.h"
#include "logger/log_module.h"
#include "util/coding.h"

namespace smartengine
{
using namespace common;
using namespace db;
using namespace util;

namespace table
{

// Helper routine: decode the next block entry starting at "p",
// storing the number of shared key bytes, non_shared key bytes,
// and the length of the value in "*shared", "*non_shared", and
// "*value_length", respectively.  Will not derefence past "limit".
//
// If any errors are detected, returns nullptr.  Otherwise, returns a
// pointer to the key delta (just past the three decoded values).
static inline const char* DecodeEntry(const char* p, const char* limit,
                                      uint32_t* shared, uint32_t* non_shared,
                                      uint32_t* value_length) {
  if (limit - p < 3) return nullptr;
  *shared = reinterpret_cast<const unsigned char*>(p)[0];
  *non_shared = reinterpret_cast<const unsigned char*>(p)[1];
  *value_length = reinterpret_cast<const unsigned char*>(p)[2];
  if ((*shared | *non_shared | *value_length) < 128) {
    // Fast path: all three values are encoded in one byte each
    p += 3;
  } else {
    if ((p = GetVarint32Ptr(p, limit, shared)) == nullptr) return nullptr;
    if ((p = GetVarint32Ptr(p, limit, non_shared)) == nullptr) return nullptr;
    if ((p = GetVarint32Ptr(p, limit, value_length)) == nullptr) return nullptr;
  }

  if (static_cast<uint32_t>(limit - p) < (*non_shared + *value_length)) {
    return nullptr;
  }
  return p;
}

int RowBlockIterator::setup(const util::Comparator *comparator,
                       const RowBlock *block,
                       const bool is_index_block)
{
  int ret = Status::kOk;

  if (IS_NULL(comparator) || UNLIKELY(!block->is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(comparator), K(*block));
  } else {
    comparator_ = comparator;
    block_ = block;
    is_index_block_ = is_index_block;

    block_->restarts_info(restarts_count_, restarts_offset_);
    current_ = restarts_offset_;
    restart_index_ = restarts_count_;
    last_ = restarts_offset_;
    is_index_block_ = is_index_block;
    prev_entries_idx_ = -1;
  }

  return ret;
}

void RowBlockIterator::reset()
{
  comparator_ = nullptr;
  block_ = nullptr;
  restarts_count_ = 0;
  restarts_offset_ = 0;
  current_ = 0;
  restart_index_ = 0;
  status_ = 0;
  last_ = 0;
  is_index_block_ = false;
  prev_entries_idx_ = -1;
  InternalIterator::reset();
}

void RowBlockIterator::Next() {
  assert(Valid());
  ParseNextKey();
  is_boundary_ = (last_ == current_) && need_seek_end_key_;
}

void RowBlockIterator::Prev() {
  assert(Valid());

  assert(prev_entries_idx_ == -1 ||
         static_cast<size_t>(prev_entries_idx_) < prev_entries_.size());
  // Check if we can use cached prev_entries_
  if (prev_entries_idx_ > 0 &&
      prev_entries_[prev_entries_idx_].offset == current_) {
    // Read cached CachedPrevEntry
    prev_entries_idx_--;
    const CachedPrevEntry& current_prev_entry =
        prev_entries_[prev_entries_idx_];

    const char* key_ptr = nullptr;
    if (current_prev_entry.key_ptr != nullptr) {
      // The key is not delta encoded and stored in the data block
      key_ptr = current_prev_entry.key_ptr;
      key_pinned_ = true;
    } else {
      // The key is delta encoded and stored in prev_entries_keys_buff_
      key_ptr = prev_entries_keys_buff_.data() + current_prev_entry.key_offset;
      key_pinned_ = false;
    }
    const Slice current_key(key_ptr, current_prev_entry.key_size);

    current_ = current_prev_entry.offset;
    key_.SetInternalKey(current_key, false /* copy */);
    value_ = current_prev_entry.value;

    return;
  }

  // Clear prev entries cache
  prev_entries_idx_ = -1;
  prev_entries_.clear();
  prev_entries_keys_buff_.clear();

  // Scan backwards to a restart point before current_
  const uint32_t original = current_;
  while (GetRestartPoint(restart_index_) >= original) {
    if (restart_index_ == 0) {
      // No more entries
      current_ = restarts_offset_;
      restart_index_ = restarts_count_;
      return;
    }
    restart_index_--;
  }

  SeekToRestartPoint(restart_index_);

  do {
    if (!ParseNextKey()) {
      break;
    }
    Slice current_key = key();

    if (key_.IsKeyPinned()) {
      // The key is not delta encoded
      prev_entries_.emplace_back(current_, current_key.data(), 0,
                                 current_key.size(), value());
    } else {
      // The key is delta encoded, cache decoded key in buffer
      size_t new_key_offset = prev_entries_keys_buff_.size();
      prev_entries_keys_buff_.append(current_key.data(), current_key.size());

      prev_entries_.emplace_back(current_, nullptr, new_key_offset,
                                 current_key.size(), value());
    }
    // Loop until end of current entry hits the start of original entry
  } while (NextEntryOffset() < original);
  prev_entries_idx_ = static_cast<int32_t>(prev_entries_.size()) - 1;
}

void RowBlockIterator::seek_end_key()
{
  // If the end key push is not set, then the end of block data,
  // which is the starting position of the restart array, will
  // be used as the upper boundary.
  if (!need_seek_end_key_ || 0 == end_ikey_.size()) {
    last_ = restarts_offset_;
  } else {
    // If the end key push is set, then find the first restart block
    // whose first key is greater than or equal to the endkey.
    uint32_t index = 0;
    bool ok = false;
    // First, find the last restart block whose first key is less than
    // endkey, which means the key of next restart point is larger than
    // target, or the first restart point with a key = target.
    ok = BinarySeek(end_ikey_, restart_index_, restarts_count_ - 1, &index);

    if (!ok) {
      return;
    }

    // Linar search after the restart index setted by upper BinarySeek, and
    // find the first block whose first key is greater than or equal to endkey,
    // which will be used as the upper boundary.
    uint32_t offset = GetRestartPoint(index);
    Slice value = Slice(block_->data_ + offset, 0);
    IterKey key;
    key.Clear();
    uint32_t current = 0;
    uint32_t restart_index = 0;
    bool key_pinned = false;
    last_ = current;

    // Linear search (within restart block) for first key >= target
    while (true) {
      if (!parse_next_key(current, restart_index, key, value, key_pinned)) {
        last_ = current;
        return;
      } else if (Compare(key.GetInternalKey(), end_ikey_) >= 0) {
        last_ = current;
        is_boundary_ = (last_ == current_) && need_seek_end_key_;
        return;
      //} else {
        //last_ = current;
      }
    }
  }
}

void RowBlockIterator::Seek(const Slice& target) {
  if (block_->data_ == nullptr) {  // Not init yet
    return;
  }
  uint32_t index = 0;
  bool ok = false;
  // seek to lower bound, set restart_index_.
  ok = BinarySeek(target, 0, restarts_count_ - 1, &index);
  if (!ok) {
    return;
  }
  SeekToRestartPoint(index);

  // seek to upper bound, set last_.
  // Linear search (within restart block) for first key >= target
  while (true) {
    if (!ParseNextKey()) {
      return;
    } else if (Compare(key_.GetInternalKey(), target) >= 0) {
      seek_end_key();
      return;
    }
  }
}

void RowBlockIterator::SeekForPrev(const Slice& target) {
  if (block_->data_ == nullptr) {  // Not init yet
    return;
  }
  uint32_t index = 0;
  bool ok = false;
  ok = BinarySeek(target, 0, restarts_count_ - 1, &index);

  if (!ok) {
    return;
  }
  SeekToRestartPoint(index);
  // Linear search (within restart block) for first key >= target

  while (ParseNextKey() && Compare(key_.GetInternalKey(), target) < 0) {
  }
  if (!Valid()) {
    SeekToLast();
  } else {
    while (Valid() && Compare(key_.GetInternalKey(), target) > 0) {
      Prev();
    }
  }
}

void RowBlockIterator::SeekToFirst() {
  if (block_->data_ == nullptr) {  // Not init yet
    return;
  }
  // seek to lower bound
  SeekToRestartPoint(0);
  ParseNextKey();
  // seek to upper bound
  seek_end_key();
}

void RowBlockIterator::SeekToLast() {
  if (block_->data_ == nullptr) {  // Not init yet
    return;
  }
  SeekToRestartPoint(restarts_count_ - 1);
  while (ParseNextKey() && NextEntryOffset() < restarts_offset_) {
    // Keep skipping
  }
}

void RowBlockIterator::CorruptionError() {
  current_ = restarts_offset_;
  restart_index_ = restarts_count_;
  status_ = Status::Corruption("bad entry in block");
  key_.Clear();
  value_.clear();
}

bool RowBlockIterator::ParseNextKey() {
  current_ = NextEntryOffset();
  const char* p = block_->data_ + current_;
  const char* limit = block_->data_ + restarts_offset_;  // Restarts come right after data
  if (p >= limit) {
    // No more entries to return.  Mark as invalid.
    current_ = restarts_offset_;
    restart_index_ = restarts_count_;
    return false;
  }

  // Decode next entry
  uint32_t shared, non_shared, value_length;
  p = DecodeEntry(p, limit, &shared, &non_shared, &value_length);
  if (p == nullptr || key_.Size() < shared) {
    CorruptionError();
    return false;
  } else {
    if (shared == 0) {
      // If this key dont share any bytes with prev key then we dont need
      // to decode it and can use it's address in the block directly.
      key_.SetInternalKey(Slice(p, non_shared), false /* copy */);
      key_pinned_ = true;
    } else {
      // This key share `shared` bytes with prev key, we need to decode it
      key_.TrimAppend(shared, p, non_shared);
      key_pinned_ = false;
    }

    value_ = Slice(p + non_shared, value_length);
    // If the current key has move to next restart block, advance the restart index.
    while (restart_index_ + 1 < restarts_count_ &&
           GetRestartPoint(restart_index_ + 1) < current_) {
      ++restart_index_;
    }
    return true;
  }
}

bool RowBlockIterator::parse_next_key(uint32_t& current,
                               uint32_t& restart_index,
                               IterKey& key,
                               Slice& value,
                               bool& key_pinned) {
  current = next_entry_offset(value);
  const char* p = block_->data_ + current;
  const char* limit = block_->data_ + restarts_offset_;  // Restarts come right after data
  if (p >= limit) {
    // No more entries to return.  Mark as invalid.
    current = restarts_offset_;
    restart_index = restarts_count_;
    return false;
  }

  // Decode next entry
  uint32_t shared = 0;
  uint32_t non_shared = 0;
  uint32_t value_length = 0;
  p = DecodeEntry(p, limit, &shared, &non_shared, &value_length);
  if (p == nullptr || key.Size() < shared) {
    CorruptionError();
    return false;
  } else {
    if (shared == 0) {
      // If this key dont share any bytes with prev key then we dont need
      // to decode it and can use it's address in the block directly.
      key.SetInternalKey(Slice(p, non_shared), false /* copy */);
      key_pinned = true;
    } else {
      // This key share `shared` bytes with prev key, we need to decode it
      key.TrimAppend(shared, p, non_shared);
      key_pinned = false;
    }

    value = Slice(p + non_shared, value_length);
    while (restart_index + 1 < restarts_count_ &&
        GetRestartPoint(restart_index + 1) < current) {
      ++restart_index;
    }
    return true;
  }
}

// Binary search in restart array to find the first restart point that
// is either the last restart point with a key less than target,
// which means the key of next restart point is larger than target, or
// the first restart point with a key = target
bool RowBlockIterator::BinarySeek(const Slice& target, uint32_t left, uint32_t right,
                           uint32_t* index) {
  assert(left <= right);

  while (left < right) {
    uint32_t mid = (left + right + 1) / 2;
    uint32_t region_offset = GetRestartPoint(mid);
    uint32_t shared, non_shared, value_length;
    const char* key_ptr = DecodeEntry(block_->data_ + region_offset,
                                      block_->data_ + restarts_offset_,
                                      &shared,
                                      &non_shared,
                                      &value_length);
    if (key_ptr == nullptr || (shared != 0)) {
      CorruptionError();
      return false;
    }
    Slice mid_key(key_ptr, non_shared);
    int cmp = Compare(mid_key, target);
    if (cmp < 0) {
      // Key at "mid" is smaller than "target". Therefore all
      // blocks before "mid" are uninteresting.
      left = mid;
    } else if (cmp > 0) {
      // Key at "mid" is >= "target". Therefore all blocks at or
      // after "mid" are uninteresting.
      right = mid - 1;
    } else {
      left = right = mid;
    }
  }

  *index = left;
  return true;
}

} // namespace table
} // namespace smartengine