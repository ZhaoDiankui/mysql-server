//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "compact/flush_iterator.h"
#include "logger/log_module.h"
#include "table/internal_iterator.h"

namespace smartengine {
using namespace common;
using namespace util;
using namespace table;
using namespace db;
using namespace memory;

namespace storage {

FlushIterator::FlushIterator()
    : is_inited_(false),
      inner_ret_(Status::kOk),
      valid_(false),
      input_(nullptr),
      cmp_(nullptr),
      snapshots_(nullptr),
      earliest_write_conflict_snapshot_(0),
      key_(),
      value_(),
      ikey_(),
      has_current_user_key_(false),
      at_next_(false),
      current_key_(),
      current_user_key_(),
      current_user_key_sequence_(0),
      current_user_key_snapshot_(0),
      has_outputted_key_(false),
      clear_and_output_next_key_(false),
      iter_stats_(),
      background_disable_merge_(false)
{}

FlushIterator::~FlushIterator()
{}

int FlushIterator::init(InternalIterator *iter,
                        const Comparator *cmp,
                        const std::vector<SequenceNumber> *snapshots,
                        const SequenceNumber earliest_write_conflict_snapshot,
                        const bool background_disable_merge)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    FLUSH_LOG(WARN, "FlushIterator has been inited", K(ret));
  } else if (IS_NULL(iter) || IS_NULL(cmp) || IS_NULL(snapshots)) {
    ret = Status::kInvalidArgument;
    FLUSH_LOG(WARN, "invalid argument", K(ret), KP(iter), KP(cmp), KP(snapshots));
  } else {
    input_ = iter;
    cmp_ = cmp;
    snapshots_ = snapshots;
    earliest_write_conflict_snapshot_ = earliest_write_conflict_snapshot;
    background_disable_merge_ = background_disable_merge;
    
    is_inited_ = true;
  }

  return ret;
}

int FlushIterator::SeekToFirst()
{ 
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    FLUSH_LOG(WARN, "FlushIterator should be inited first", K(ret));
  } else {
    input_->SeekToFirst();
    inner_next();
  }

  return ret;
}

int FlushIterator::Next()
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    FLUSH_LOG(WARN, "FlushIterator should be inited first", K(ret));
  } else {
    if (!at_next_) {
      input_->Next();
      inner_ret_ = input_->status().code();
    }

    inner_next();

    if (valid_) {
      // Record that we've outputted a record for the current key.
      has_outputted_key_ = true;
    }
  }

  return ret;
}

void FlushIterator::inner_next()
{
  at_next_ = false;
  valid_ = false;

  while (!valid_ && (Status::kOk == inner_ret_) && input_->Valid()) {
    key_ = input_->key();
    value_ = input_->value();
    iter_stats_.num_input_records++;

    if (!ParseInternalKey(key_, &ikey_)) {
      se_assert(false);
      inner_ret_ = Status::kCorruption;
      break;
    }

    // Update input statistics
    if (ikey_.type == kTypeDeletion || ikey_.type == kTypeSingleDeletion) {
      iter_stats_.num_input_deletion_records++;
    }
    iter_stats_.total_input_raw_key_bytes += key_.size();
    iter_stats_.total_input_raw_value_bytes += value_.size();

    // Check whether the user key changed. After this if statement current_key_
    // is a copy of the current input key, ikey_.user_key is pointing to the copy.
    if (!has_current_user_key_ || !cmp_->Equal(ikey_.user_key, current_user_key_)) {
      // First occurrence of this user key, copy key for output
      key_ = current_key_.SetInternalKey(key_, &ikey_);
      current_user_key_ = ikey_.user_key;
      has_current_user_key_ = true;
      has_outputted_key_ = false;
      current_user_key_sequence_ = kMaxSequenceNumber;
      current_user_key_snapshot_ = 0;
    } else {
      // Update the current key to reflect the new sequence number/type without
      // copying the user key.
      current_key_.UpdateInternalKey(ikey_.sequence, ikey_.type);
      key_ = current_key_.GetInternalKey();
      ikey_.user_key = current_key_.GetUserKey();
    }

    //when background_disable_merge_ is true, that means it's new subtable, and
    //we should maintain all records before index-build ready.
    if (background_disable_merge_) {
       valid_ = true;
       inner_ret_ = input_->status().code();
       break;
    }

    // If there are no snapshots, then this kv affect visibility at tip.
    // Otherwise, search though all existing snapshots to find the earliest
    // snapshot that is affected by this kv.
    SequenceNumber last_sequence = current_user_key_sequence_;
    current_user_key_sequence_ = ikey_.sequence;
    SequenceNumber last_snapshot = current_user_key_snapshot_;
    SequenceNumber prev_snapshot = 0;  // 0 means no previous snapshot
    current_user_key_snapshot_ = snapshots_->empty() ? kMaxSequenceNumber
        : findEarliestVisibleSnapshot(ikey_.sequence, &prev_snapshot);

    if (clear_and_output_next_key_) {
        // In the previous iteration we encountered a single delete that we could
        // not compact out.  We will keep this Put, but can drop it's data.
        assert(ikey_.type == kTypeValue || kTypeValueLarge == ikey_.type);
        assert(current_user_key_snapshot_ == last_snapshot);

        value_.clear();
        valid_ = true;
        clear_and_output_next_key_ = false;
    } else if (ikey_.type == kTypeSingleDeletion) {
      deal_single_deletion(prev_snapshot);
    } else if (last_snapshot == current_user_key_snapshot_) {
      // If the earliest snapshot is which this key is visible in
      // is the same as the visibility of a previous instance of the
      // same key, then this kv is not visible in any snapshot.
      // Hidden by an newer entry for same user key
      // Note: Dropping this key will not affect TransactionDB write-conflict
      // checking since there has already been a record returned for this key
      // in this snapshot.
      assert(last_sequence >= current_user_key_sequence_);
      ++iter_stats_.num_record_drop_hidden;  // (A)
      input_->Next();
    } else {
      // 1. new user key -OR-
      // 2. different snapshot stripe
      valid_ = true;
    }

    inner_ret_ = input_->status().code();
  }
}

inline SequenceNumber FlushIterator::findEarliestVisibleSnapshot(
    SequenceNumber in, SequenceNumber* prev_snapshot) {
  assert(snapshots_->size());
  SequenceNumber prev __attribute__((__unused__)) = kMaxSequenceNumber;
  for (const auto cur : *snapshots_) {
    assert(prev == kMaxSequenceNumber || prev <= cur);
    if (cur >= in) {
      *prev_snapshot = prev == kMaxSequenceNumber ? 0 : prev;
      return cur;
    }
    prev = cur;
    assert(prev < kMaxSequenceNumber);
  }
  *prev_snapshot = prev;
  return kMaxSequenceNumber;
}

void FlushIterator::deal_single_deletion(SequenceNumber prev_snapshot)
{
  // We can compact out a SingleDelete if:
  // 1) We encounter the corresponding PUT -OR- we know that this key
  //    doesn't appear past this output level
  // =AND=
  // 2) We've already returned a record in this snapshot -OR-
  //    there are no earlier earliest_write_conflict_snapshot.
  //
  // Rule 1 is needed for SingleDelete correctness.  Rule 2 is needed to
  // allow Transactions to do write-conflict checking (if we compacted away
  // all keys, then we wouldn't know that a write happened in this
  // snapshot).  If there is no earlier snapshot, then we know that there
  // are no active transactions that need to know about any writes.
  //
  // Optimization 3:
  // If we encounter a SingleDelete followed by a PUT and Rule 2 is NOT
  // true, then we must output a SingleDelete.  In this case, we will decide
  // to also output the PUT.  While we are compacting less by outputting the
  // PUT now, hopefully this will lead to better compaction in the future
  // when Rule 2 is later true (Ie, We are hoping we can later compact out
  // both the SingleDelete and the Put, while we couldn't if we only
  // outputted the SingleDelete now).
  // In this case, we can save space by removing the PUT's value as it will
  // never be read.
  //
  // Deletes and Merges are not supported on the same key that has a
  // SingleDelete as it is not possible to correctly do any partial
  // compaction of such a combination of operations.  The result of mixing
  // those operations for a given key is documented as being undefined.  So
  // we can choose how to handle such a combinations of operations.  We will
  // try to compact out as much as we can in these cases.
  // We will report counts on these anomalous cases.

  // The easiest way to process a SingleDelete during iteration is to peek
  // ahead at the next key.
  ParsedInternalKey next_ikey;
  input_->Next();

  // Check whether the next key exists, is not corrupt, and is the same key
  // as the single delete.
  if (input_->Valid() && ParseInternalKey(input_->key(), &next_ikey) &&
      cmp_->Equal(ikey_.user_key, next_ikey.user_key)) {
    // Check whether the next key belongs to the same snapshot as the
    // SingleDelete.
    if (prev_snapshot == 0 || next_ikey.sequence > prev_snapshot) {
      if (next_ikey.type == kTypeSingleDeletion) {
        // We encountered two SingleDeletes in a row.  This could be due to
        // unexpected user input.
        // Skip the first SingleDelete and let the next iteration decide how
        // to handle the second SingleDelete

        // First SingleDelete has been skipped since we already called
        // input_->Next().
        ++iter_stats_.num_record_drop_obsolete;
        ++iter_stats_.num_single_del_mismatch;
      } else if ((ikey_.sequence <= earliest_write_conflict_snapshot_) ||
                 has_outputted_key_) {
        // Found a matching value, we can drop the single delete and the
        // value.  It is safe to drop both records since we've already
        // outputted a key in this snapshot, or there is no earlier
        // snapshot (Rule 2 above).

        // Note: it doesn't matter whether the second key is a Put or if it
        // is an unexpected Merge or Delete.  We will compact it out
        // either way. We will maintain counts of how many mismatches
        // happened
        if (next_ikey.type != kTypeValue) {
          ++iter_stats_.num_single_del_mismatch;
        }

        ++iter_stats_.num_record_drop_hidden;
        ++iter_stats_.num_record_drop_obsolete;
        // Already called input_->Next() once.  Call it a second time to
        // skip past the second key.
        input_->Next();
      } else {
        // Found a matching value, but we cannot drop both keys since
        // there is an earlier snapshot and we need to leave behind a record
        // to know that a write happened in this snapshot (Rule 2 above).
        // Clear the value and output the SingleDelete. (The value will be
        // outputted on the next iteration.)

        // Setting valid_ to true will output the current SingleDelete
        valid_ = true;

        // Set up the Put to be outputted in the next iteration.
        // (Optimization 3).
        clear_and_output_next_key_ = true;
      }
    } else {
      // We hit the next snapshot without hitting a put, so the iterator
      // returns the single delete.
      valid_ = true;
    }
  } else {
    // We are at the end of the input, could not parse the next key, or hit
    // a different key. The iterator returns the single delete if the key
    // possibly exists beyond the current output level.  We set
    // has_current_user_key to false so that if the iterator is at the next
    // key, we do not compare it again against the previous key at the next
    // iteration. If the next key is corrupt, we return before the
    // comparison, so the value of has_current_user_key does not matter.
    has_current_user_key_ = false;
    // Output SingleDelete
    valid_ = true;
  }

  if (valid_) {
    at_next_ = true;
  }

}

}  // namespace db
}  // namespace smartengine
