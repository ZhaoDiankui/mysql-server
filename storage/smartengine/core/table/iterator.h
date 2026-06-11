/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// An iterator yields a sequence of key/value pairs from a source.
// The following class defines the interface.  Multiple implementations
// are provided by this library.  In particular, iterators are provided
// to access the contents of a Table or a DB.
//
// Multiple threads can invoke const methods on an Iterator without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Iterator must use
// external synchronization.

#ifndef STORAGE_ROCKSDB_INCLUDE_ITERATOR_H_
#define STORAGE_ROCKSDB_INCLUDE_ITERATOR_H_

#include <string>
#include "util/status.h"
#include "util/to_string.h"
#include "util/types.h"

namespace smartengine {
namespace common {
class Cleanable;
}

namespace db {

struct KeyRange
{
  KeyRange() {}
  KeyRange(const common::Slice &s,
           const common::Slice &e,
           const bool start_key_inclusive,
           const bool end_key_inclusive)
      : start_key_(s),
        end_key_(e),
        start_key_inclusive_(start_key_inclusive),
        end_key_inclusive_(end_key_inclusive)
  {}
  common::Slice start_key_;
  common::Slice end_key_;
  bool start_key_inclusive_;
  bool end_key_inclusive_;

  DECLARE_AND_DEFINE_TO_STRING(KV(start_key_),
                               KV(start_key_inclusive_),
                               KV(end_key_),
                               KV(end_key_inclusive_))
};

class Iterator : public common::Cleanable {
 public:
  enum RecordStatus { kExist, kDeleted, kNonExist/*never exist*/,kError };
  Iterator() {}
  virtual ~Iterator() {}

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

  // Position at the last key in the source that at or before target
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or before target.
  virtual void SeekForPrev(const common::Slice& target) {}

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
  // satisfied without doing some IO, then this returns Status::Incomplete().
  virtual common::Status status() const = 0;

  virtual int set_end_key(const common::Slice& end_key_slice)
  {
    return common::Status::kNotSupported;
  }

  virtual common::SequenceNumber key_seq() const;

  virtual RecordStatus key_status() const { return kNonExist; }

  virtual bool for_unique_check() const { return false; }

 private:
  // No copying allowed
  Iterator(const Iterator&);
  void operator=(const Iterator&);
};

// Return an empty iterator (yields nothing).
extern Iterator* NewEmptyIterator();

// Return an empty iterator with the specified status.
extern Iterator* NewErrorIterator(const common::Status& status);

}  // namespace db
}  // namespace smartengine
#endif  // STORAGE_ROCKSDB_INCLUDE_ITERATOR_H_
