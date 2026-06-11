//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#pragma once

#include "table/iterator.h"

#include "util/comparator.h"

namespace smartengine {
namespace common {
class Cleanable;
}

namespace db {
class PinnedIteratorsManager;
}

namespace table {

typedef uint64_t RowSource;
#define ROW_SOURCE_UNKNOWN (-1UL)
#define ROW_SOURCE_MEMTABLE (-2UL)

class InternalIterator : public common::Cleanable {
 public:
  InternalIterator() {}
  virtual ~InternalIterator() {}

  // An iterator is either positioned at a key/value pair, or
  // not valid.  This method returns true iff the iterator is valid.
  virtual bool Valid() const = 0;

  // Position at the first key in the source.  The iterator is Valid()
  // after this call iff the source is not empty.
  virtual void SeekToFirst() = 0;

  // Position at the last key in the source.  The iterator is
  // Valid() after this call iff the source is not empty.
  virtual void SeekToLast() = 0;

  // Position at the first key in the source that at or past target
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or past target.
  virtual void Seek(const common::Slice& target) = 0;

  // Position at the first key in the source that at or before target
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or before target.
  virtual void SeekForPrev(const common::Slice& target) = 0;

  // Moves to the next entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the last entry in the source.
  // REQUIRES: Valid()
  virtual void Next() = 0;

  // Moves to the previous entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the first entry in source.
  // REQUIRES: Valid()
  virtual void Prev() = 0;

  // Return the key for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: Valid()
  virtual common::Slice key() const = 0;

  // Return the value for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: !AtEnd() && !AtStart()
  virtual common::Slice value() const = 0;

  // If an error has occurred, return it.  Else return an ok status.
  // If non-blocking IO is requested and this operation cannot be
  // satisfied without doing some IO, then this returns
  // common::Status::Incomplete().
  virtual common::Status status() const = 0;

  // Pass the PinnedIteratorsManager to the Iterator, most Iterators dont
  // communicate with PinnedIteratorsManager so default implementation is no-op
  // but for Iterators that need to communicate with PinnedIteratorsManager
  // they will implement this function and use the passed pointer to communicate
  // with PinnedIteratorsManager.
  virtual void SetPinnedItersMgr(db::PinnedIteratorsManager* pinned_iters_mgr) {
  }

  // If true, this means that the common::Slice returned by key() is valid as
  // long as
  // PinnedIteratorsManager::ReleasePinnedData is not called and the
  // Iterator is not deleted.
  //
  // IsKeyPinned() is guaranteed to always return true if
  //  - Iterator is created with common::ReadOptions::pin_data = true
  //  - DB tables were created with BlockBasedTableOptions::use_delta_encoding
  //    set to false.
  virtual bool IsKeyPinned() const { return false; }

  // If true, this means that the common::Slice returned by value() is valid as
  // long as
  // PinnedIteratorsManager::ReleasePinnedData is not called and the
  // Iterator is not deleted.
  virtual bool IsValuePinned() const { return false; }

  virtual void set_end_key(const common::Slice& end_ikey, const bool need_seek_end_key)
  {
    end_ikey_ = end_ikey;
    need_seek_end_key_ = need_seek_end_key;
  }

  const common::Slice &get_end_key() const { return end_ikey_; }

  bool get_is_boundary() const  { return is_boundary_; }

  bool get_need_seek_end_key() const  { return need_seek_end_key_; }

  virtual RowSource get_source() const { return row_source_; }

  void set_source(const RowSource &source) { row_source_ = source; }

 protected:
  void SeekForPrevImpl(const common::Slice& target,
                       const util::Comparator* cmp) {
    Seek(target);
    if (!Valid()) {
      SeekToLast();
    }
    while (Valid() && cmp->Compare(target, key()) < 0) {
      Prev();
    }
  }

  void reset()
  {
    end_ikey_.clear();
    is_boundary_ = false;
    need_seek_end_key_ = false;
    row_source_ = ROW_SOURCE_UNKNOWN;
  }

protected:
  // If the end key push down is set, the end_ikey_ is set to upper
  // boundary of range scan. And the end_ikey_ is not includes in the
  // result.For example, if the range condition is "a > 1 and a < 10",
  // the endkey_ is set to "10". If the range condition is "a > 1 and
  // a <= 10", the end_ikey_ is set to "11"(11 may not exist actually).
  // The detailed setup logic is located in ha_smartengine::index_read_map_impl::
  // setup_scan_iterator.
  common::Slice end_ikey_;
  bool is_boundary_ = false;
  bool need_seek_end_key_ = false;
  RowSource row_source_;

 private:
  // No copying allowed
  InternalIterator(const InternalIterator&) = delete;
  InternalIterator& operator=(const InternalIterator&) = delete;
};

// Return an empty iterator (yields nothing).
extern InternalIterator* NewEmptyInternalIterator();

// Return an empty iterator with the specified status.
extern InternalIterator* NewErrorInternalIterator(const common::Status& status);

}  // namespace table
}  // namespace smartengine
