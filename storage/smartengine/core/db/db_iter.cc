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

#include "db/db_iter.h"
#include <string>

#include "db/dbformat.h"
#include "db/pinned_iterators_manager.h"
#include "logger/log_module.h"
#include "monitoring/query_perf_context.h"
#include "options/options.h"
#include "table/internal_iterator.h"
#include "table/large_object.h"
#include "util/arena.h"
#include "util/file_name.h"
#include "util/string_util.h"

namespace smartengine {
using namespace monitor;
using namespace util;
using namespace common;
using namespace table;
using namespace storage;

namespace db {

// Memtables and sstables that make the DB representation contain
// (userkey,seq,type) => uservalue entries.  DBIter
// combines multiple entries for the same userkey found in the DB
// representation into a single entry while accounting for sequence
// numbers, deletion markers, overwrites, etc.
class DBIter : public Iterator {
public:
 // The following is grossly complicated. TODO: clean it up
 // Which direction is the iterator currently moving?
 // (1) When moving forward, the internal iterator is positioned at
 //     the exact entry that yields this->key(), this->value()
 // (2) When moving backwards, the internal iterator is positioned
 //     just before all entries whose user key == this->key().
 enum Direction { kForward, kReverse };

 DBIter(Env* env,
        const ReadOptions& read_options,
        const ImmutableCFOptions& cf_options,
        const Comparator* cmp,
        InternalIterator* iter,
        SequenceNumber s,
        bool arena_mode,
        uint64_t max_sequential_skip_in_iterations)
     : arena_mode_(arena_mode),
       env_(env),
       user_comparator_(cmp),
       iter_(iter),
       sequence_(s),
       large_value_(),
       direction_(kForward),
       valid_(false),
       statistics_(cf_options.statistics),
       iterate_upper_bound_(read_options.iterate_upper_bound),
       pin_thru_lifetime_(read_options.pin_data),
       key_sequence_(kMaxSequenceNumber),
       skip_del_(read_options.skip_del_),
       is_art_based_memtable_(std::string(cf_options.memtable_factory->Name()) == "ARTFactory") {
   max_skip_ = max_sequential_skip_in_iterations;
   max_skippable_internal_keys_ = read_options.max_skippable_internal_keys;
   if (pin_thru_lifetime_) {
     pinned_iters_mgr_.StartPinning();
   }
   if (iter_) {
     iter_->SetPinnedItersMgr(&pinned_iters_mgr_);
   }
 }
 virtual ~DBIter() override
 {
   // Release pinned data if any
   if (pinned_iters_mgr_.PinningEnabled()) {
     pinned_iters_mgr_.ReleasePinnedData();
   }

   if (!arena_mode_) {
     MOD_DELETE_OBJECT(InternalIterator, iter_);
   } else {
     iter_->~InternalIterator();
   }
 }
 virtual void SetIter(InternalIterator* iter) {
   assert(iter_ == nullptr);
   iter_ = iter;
   iter_->SetPinnedItersMgr(&pinned_iters_mgr_);
 }

 virtual bool Valid() const override { return valid_; }
 virtual Slice key() const override {
   assert(valid_);
   return saved_key_.GetUserKeyFromUserKey();
 }
 virtual Slice value() const override {
   assert(valid_);
   Slice plain_value;
   ValueType type = kTypeValue;
   if (direction_ == kReverse) {
     plain_value = pinned_value_;
     type = pinned_key_type_;
   } else {
     plain_value = iter_->value();
     type = ExtractValueType(iter_->key());
   }
   if (LIKELY(type != kTypeValueLarge)) {
     return plain_value;
   }

   // retrieve data from extents for large object
   int ret = Status::kOk;
   Slice result;
   large_value_.reuse();
   if (FAILED(large_value_.convert_to_normal_format(plain_value, result))) {
    SE_LOG(WARN, "fail to covert large value to normal format", K(ret));
   }

   return result;
 }

 virtual Status status() const override {
   if (status_.ok()) {
     return iter_->status();
   } else {
     return status_;
   }
 }

 virtual void Next() override;
 virtual void Prev() override;
 virtual void Seek(const Slice& target) override;
 virtual void SeekForPrev(const Slice& target) override;
 virtual void SeekToFirst() override;
 virtual void SeekToLast() override;
 virtual int set_end_key(const Slice& end_key_slice) override;
 virtual SequenceNumber key_seq() const override
 {
   return key_sequence_;
 }

protected:
 void ReverseToForward();
 void ReverseToBackward();
 void PrevInternal();
 void FindParseableKey(ParsedInternalKey* ikey, Direction direction);
 bool FindValueForCurrentKey();
 bool FindValueForCurrentKeyUsingSeek();
 void FindPrevUserKey();
 void FindNextUserKey();
 inline void FindNextUserEntry(bool skipping);
 void FindNextUserEntryInternal(bool skipping, bool uniq_check, bool &deleted);
 bool ParseKey(ParsedInternalKey* key);
 bool TooManyInternalKeysSkipped(bool increment = true);

 // Temporarily pin the blocks that we encounter until ReleaseTempPinnedData()
 // is called
 void TempPinData() {
   if (!pin_thru_lifetime_) {
     pinned_iters_mgr_.StartPinning();
   }
 }

 // Release blocks pinned by TempPinData()
 void ReleaseTempPinnedData() {
   if (!pin_thru_lifetime_ && pinned_iters_mgr_.PinningEnabled()) {
     pinned_iters_mgr_.ReleasePinnedData();
   }
 }

 inline void ClearSavedValue() {
   if (saved_value_.capacity() > 1048576) {
     std::string empty;
     swap(empty, saved_value_);
   } else {
     saved_value_.clear();
   }
 }

 inline void ResetInternalKeysSkippedCounter() {
   num_internal_keys_skipped_ = 0;
 }

protected:
 bool arena_mode_;
 Env* const env_;
 const Comparator* const user_comparator_;
 InternalIterator* iter_;
 SequenceNumber const sequence_;

 Status status_;
 IterKey saved_key_;
 IterKey saved_end_key_;
 std::string saved_value_;
 mutable LargeValue large_value_;
 Slice pinned_value_;
 ValueType pinned_key_type_;  // key type of pinned_value_
 Direction direction_;
 bool valid_;
 // for prefix seek mode to support prev()
 Statistics* statistics_;
 uint64_t max_skip_;
 uint64_t max_skippable_internal_keys_;
 uint64_t num_internal_keys_skipped_;
 const Slice* iterate_upper_bound_;
 // Means that we will pin all data blocks we read as long the Iterator
 // is not deleted, will be true if ReadOptions::pin_data is true
 const bool pin_thru_lifetime_;
 PinnedIteratorsManager pinned_iters_mgr_;
 SequenceNumber key_sequence_;
 const bool skip_del_;
 bool is_art_based_memtable_;

private:
 // No copying allowed
 DBIter(const DBIter&);
 void operator=(const DBIter&);
};

class UniqueCheckDBIterator : public DBIter {
public:
  UniqueCheckDBIterator(Env* env,
                        const ReadOptions& read_options,
                        const ImmutableCFOptions& cf_options,
                        const Comparator* cmp,
                        InternalIterator* iter,
                        SequenceNumber s,
                        bool arena_mode,
                        uint64_t max_sequential_skip)
      : DBIter(env,
               read_options,
               cf_options,
               cmp,
               nullptr,
               s,
               arena_mode,
               max_sequential_skip)
  {
    assert(!read_options.skip_del_);
  }

  // Copied from DBIter::CheckNext
  void Next() override {
    QUERY_TRACE_SCOPE(TracePoint::DB_ITER_NEXT);
    assert(valid_);

    // Release temporarily pinned blocks from last operation
    if (UNLIKELY(!pin_thru_lifetime_ && pinned_iters_mgr_.PinningEnabled())) {
      pinned_iters_mgr_.ReleasePinnedData();
    }
    ResetInternalKeysSkippedCounter();
    if (direction_ == kReverse) {
      ReverseToForward();
    } else if (iter_->Valid()) {
      // The iter position is the current key, which is already returned. We can safely issue a
      // Next() without checking the current key.
      iter_->Next();
      QUERY_COUNT(CountPoint::INTERNAL_KEY_SKIPPED);
    }

    key_status_ = kNonExist;
    // Now we point to the next internal position.
    if (iter_->Valid()) {
      bool deleted = false;
      // valid_ will be set by FindNextUserEntryInternal
      FindNextUserEntryInternal(false, true, deleted);
      if (valid_) {
        key_status_ = deleted ? kDeleted : kExist;
      }
    }

    if (!iter_->Valid()) {
      valid_ = false;
    }
  }

  void Seek(const Slice& target) override {
    QUERY_TRACE_SCOPE(TracePoint::DB_ITER_SEEK);
    ReleaseTempPinnedData();
    ResetInternalKeysSkippedCounter();
    saved_key_.Clear();
    saved_key_.SetInternalKey(target, sequence_);
    iter_->Seek(saved_key_.GetInternalKey());

    key_status_ = kNonExist;
    if (iter_->Valid()) {
      direction_ = kForward;
      ClearSavedValue();

      bool deleted = false;
      FindNextUserEntryInternal(false, true, deleted);
      if (valid_) {

        key_status_ = deleted ? kDeleted : kExist;
      }
    } else {
      valid_ = false;
    }
  }

  void SeekToFirst() override {
    QUERY_TRACE_SCOPE(TracePoint::DB_ITER_SEEK);
    direction_ = kForward;
    ReleaseTempPinnedData();
    ResetInternalKeysSkippedCounter();
    ClearSavedValue();
    iter_->SeekToFirst();

    key_status_ = kNonExist;
    if (iter_->Valid()) {
      bool deleted = false;
      FindNextUserEntryInternal(false, true, deleted);
      if (valid_) {

        key_status_ = deleted ? kDeleted : kExist;
      }
    } else {
      valid_ = false;
    }
  }

  RecordStatus key_status() const override { return key_status_; }

  bool for_unique_check() const override { return true; }
private:
  RecordStatus key_status_ = kNonExist;
};

int DBIter::set_end_key(const Slice& end_key_slice)
{
  int ret = Status::kOk;
  if (UNLIKELY(nullptr == iter_)) {
    ret = Status::kNotInit;
  } else if (0 < end_key_slice.size()) {
    const SequenceNumber sequence = kMaxSequenceNumber;
    saved_end_key_.Clear();
    saved_end_key_.SetInternalKey(end_key_slice, sequence);
    iter_->set_end_key(saved_end_key_.GetInternalKey(), true /* need seek end key */);
  } else {
    saved_end_key_.Clear();
    iter_->set_end_key(Slice(), false /* need seek end key*/);
  }
  return ret;
}

inline bool DBIter::ParseKey(ParsedInternalKey* ikey) {
  if (!ParseInternalKey(iter_->key(), ikey)) {
    status_ = Status::Corruption("corrupted internal key in DBIter");
    __SE_LOG(ERROR, "corrupted internal key in DBIter: %s",
                    iter_->key().ToString(true).c_str());
    return false;
  } else {
    return true;
  }
}

void DBIter::Next() {
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_NEXT);
  assert(valid_);

  // Release temporarily pinned blocks from last operation
  if (UNLIKELY(!pin_thru_lifetime_ && pinned_iters_mgr_.PinningEnabled())) {
    pinned_iters_mgr_.ReleasePinnedData();
  }
  ResetInternalKeysSkippedCounter();
  if (direction_ == kReverse) {
    ReverseToForward();
  } else if (iter_->Valid()) {
    // The iter position is the current key, which is already returned. We can safely issue a
    // Next() without checking the current key.
    iter_->Next();
    QUERY_COUNT(CountPoint::INTERNAL_KEY_SKIPPED);
  }

  // Now we point to the next internal position.
  if (!iter_->Valid()) {
    valid_ = false;
    return;
  }
  FindNextUserEntry(true /* skipping the current user key */);
}

// PRE: saved_key_ has the current user key if skipping
// POST: saved_key_ should have the next user key if valid_,
//
// NOTE: In between, saved_key_ can point to a user key that has
//       a delete marker or a sequence number higher than sequence_
//       saved_key_ MUST have a proper user_key before calling this function
//
inline void DBIter::FindNextUserEntry(bool skipping) {
  bool deleted = false;
  FindNextUserEntryInternal(skipping, false, deleted);
}

// Actual implementation of DBIter::FindNextUserEntry()
// uniq_check for online build index, we need check the record  whether has been delete or not
// deleted return status of record, only used when uniq_check is true.
void DBIter::FindNextUserEntryInternal(bool skipping, bool uniq_check, bool &deleted) {
  // Loop until we hit an acceptable entry to yield
  assert(iter_->Valid());
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_NEXT_USER_ENTRY);
  assert(direction_ == kForward);
  deleted = false;

  // How many times in a row we have skipped an entry with user key less than
  // or equal to saved_key_. We could skip these entries either because
  // sequence numbers were too high or because skipping = true.
  // What saved_key_ contains throughout this method:
  //  - if skipping        : saved_key_ contains the key that we need to skip,
  //                         and we haven't seen any keys greater than that,
  //  - if num_skipped > 0 : saved_key_ contains the key that we have skipped
  //                         num_skipped times, and we haven't seen any keys
  //                         greater than that,
  //  - none of the above  : saved_key_ can contain anything, it doesn't matter.
  uint64_t num_skipped = 0;

  do {
    ParsedInternalKey ikey;

    if (!ParseKey(&ikey)) {
      // Skip corrupted keys.
      iter_->Next();
      continue;
    }

    if (iterate_upper_bound_ != nullptr &&
        user_comparator_->Compare(ikey.user_key, *iterate_upper_bound_) >= 0) {
      break;
    }

    if (TooManyInternalKeysSkipped()) {
      return;
    }

    if (ikey.sequence <= sequence_) {
      if (skipping &&
          user_comparator_->Compare(ikey.user_key, saved_key_.GetUserKey()) <=
              0) {
        num_skipped++;  // skip this entry
        QUERY_COUNT(CountPoint::INTERNAL_KEY_SKIPPED);
      } else {
        num_skipped = 0;
        key_sequence_ = ikey.sequence;
        switch (ikey.type) {
          case kTypeDeletion:
          case kTypeSingleDeletion:
            // Arrange to skip all upcoming entries for this key since
            // they are hidden by this deletion.
            saved_key_.SetUserKey(
                ikey.user_key,
                !iter_->IsKeyPinned() || !pin_thru_lifetime_ /* copy */);

            if (uniq_check || !skip_del_) {
              valid_ = true;
              if(uniq_check){
                deleted = true;
              }
              return;
            } else {
              skipping = true;
              QUERY_COUNT(CountPoint::INTERNAL_DEL_SKIPPED);
              break;
            }
          case kTypeValue:
          case kTypeValueLarge:
            saved_key_.SetUserKey(
                ikey.user_key,
                !iter_->IsKeyPinned() || !pin_thru_lifetime_ /* copy */);
            valid_ = true;
            return;
          default:
            se_assert(false);
            break;
        }
      }
    } else {
      // This key was inserted after our snapshot was taken.
      QUERY_COUNT(CountPoint::INTERNAL_UPD_SKIPPED);

      // Here saved_key_ may contain some old key, or the default empty key, or
      // key assigned by some random other method. We don't care.
      if (user_comparator_->Compare(ikey.user_key, saved_key_.GetUserKey()) <=
          0) {
        num_skipped++;
      } else {
        saved_key_.SetUserKey(
            ikey.user_key,
            !iter_->IsKeyPinned() || !pin_thru_lifetime_ /* copy */);
        skipping = false;
        num_skipped = 0;
      }
    }

    // If we have sequentially iterated via numerous equal keys, then it's
    // better to seek so that we can avoid too many key comparisons.
    if (num_skipped > max_skip_) {
      num_skipped = 0;
      std::string last_key;
      if (skipping) {
        // We're looking for the next user-key but all we see are the same
        // user-key with decreasing sequence numbers. Fast forward to
        // sequence number 0 and type deletion (the smallest type).
        AppendInternalKeyForNext(&last_key, ParsedInternalKey(saved_key_.GetUserKey(),
                                                              kMaxSequenceNumber, kTypeValueLarge));
        // Don't set skipping = false because we may still see more user-keys
        // equal to saved_key_.
      } else {
        // We saw multiple entries with this user key and sequence numbers
        // higher than sequence_. Fast forward to sequence_.
        // Note that this only covers a case when a higher key was overwritten
        // many times since our snapshot was taken, not the case when a lot of
        // different keys were inserted after our snapshot was taken.
        if (is_art_based_memtable_) {
          iter_->Next();
          continue;
        } else {
          AppendInternalKey(&last_key,
                            ParsedInternalKey(saved_key_.GetUserKey(), sequence_,
                                              kValueTypeForSeek));
        }
      }
      iter_->Seek(last_key);
      QUERY_COUNT(CountPoint::NUMBER_OF_RESEEKS_IN_ITERATION);
    } else {
      iter_->Next();
    }
  } while (iter_->Valid());
  valid_ = false;
}

void DBIter::Prev() {
  assert(valid_);
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_PREV);
  ReleaseTempPinnedData();
  ResetInternalKeysSkippedCounter();
  if (direction_ == kForward) {
    ReverseToBackward();
  }
  PrevInternal();
}

void DBIter::ReverseToForward() {
  FindNextUserKey();
  direction_ = kForward;
  if (!iter_->Valid()) {
    iter_->SeekToFirst();
  }
}

void DBIter::ReverseToBackward() {
#ifndef NDEBUG
  if (iter_->Valid()) {
    ParsedInternalKey ikey;
    assert(ParseKey(&ikey));
    assert(user_comparator_->Compare(ikey.user_key, saved_key_.GetUserKey()) <=
           0);
  }
#endif

  FindPrevUserKey();
  direction_ = kReverse;
}

void DBIter::PrevInternal() {
  if (!iter_->Valid()) {
    valid_ = false;
    return;
  }

  ParsedInternalKey ikey;

  while (iter_->Valid()) {
    saved_key_.SetUserKey(
        ExtractUserKey(iter_->key()),
        !iter_->IsKeyPinned() || !pin_thru_lifetime_ /* copy */);
    key_sequence_ = ikey.sequence;

    if (FindValueForCurrentKey()) {
      valid_ = true;
      if (!iter_->Valid()) {
        return;
      }
      FindParseableKey(&ikey, kReverse);
      if (user_comparator_->Equal(ikey.user_key, saved_key_.GetUserKey())) {
        FindPrevUserKey();
      }
      return;
    }

    if (TooManyInternalKeysSkipped(false)) {
      return;
    }

    if (!iter_->Valid()) {
      break;
    }
    FindParseableKey(&ikey, kReverse);
    if (user_comparator_->Equal(ikey.user_key, saved_key_.GetUserKey())) {
      FindPrevUserKey();
    }
  }
  // We haven't found any key - iterator is not valid
  // Or the prefix is different than start prefix
  assert(!iter_->Valid());
  valid_ = false;
}

// This function checks, if the entry with biggest sequence_number <= sequence_
// is non kTypeDeletion or kTypeSingleDeletion. If it's not, we save value in
// saved_value_
bool DBIter::FindValueForCurrentKey() {
  assert(iter_->Valid());
  ValueType last_key_entry_type = kTypeDeletion;

  ParsedInternalKey ikey;
  FindParseableKey(&ikey, kReverse);

  ReleaseTempPinnedData();
  TempPinData();
  size_t num_skipped = 0;
  while (iter_->Valid() && ikey.sequence <= sequence_ &&
         user_comparator_->Equal(ikey.user_key, saved_key_.GetUserKey())) {
    key_sequence_ = ikey.sequence;
    if (TooManyInternalKeysSkipped()) {
      return false;
    }

    // We iterate too much: let's use Seek() to avoid too much key comparisons
    if (num_skipped >= max_skip_) {
      return FindValueForCurrentKeyUsingSeek();
    }

    last_key_entry_type = ikey.type;
    switch (last_key_entry_type) {
      case kTypeValue:
      case kTypeValueLarge:
        assert(iter_->IsValuePinned());
        pinned_value_ = iter_->value();
        pinned_key_type_ = last_key_entry_type;
        break;
      case kTypeDeletion:
      case kTypeSingleDeletion:
        QUERY_COUNT(CountPoint::INTERNAL_DEL_SKIPPED);
        break;
      default:
        se_assert(false);
    }

    QUERY_COUNT(CountPoint::INTERNAL_KEY_SKIPPED);
    assert(user_comparator_->Equal(ikey.user_key, saved_key_.GetUserKey()));
    iter_->Prev();
    ++num_skipped;
    FindParseableKey(&ikey, kReverse);
  }

  Status s;
  switch (last_key_entry_type) {
    case kTypeDeletion:
    case kTypeSingleDeletion:
      valid_ = false;
      return false;
    case kTypeValue:
    case kTypeValueLarge:
      // do nothing - we've already has value in saved_value_
      break;
    default:
      se_assert(false);
      break;
  }
  valid_ = true;
  if (!s.ok()) {
    status_ = s;
  }
  return true;
}

// This function is used in FindValueForCurrentKey.
// We use Seek() function instead of Prev() to find necessary value
bool DBIter::FindValueForCurrentKeyUsingSeek() {
  // FindValueForCurrentKey will enable pinning before calling
  // FindValueForCurrentKeyUsingSeek()
  assert(pinned_iters_mgr_.PinningEnabled());
  std::string last_key;
  AppendInternalKey(&last_key, ParsedInternalKey(saved_key_.GetUserKey(),
                                                 sequence_, kValueTypeForSeek));
  iter_->Seek(last_key);
  QUERY_COUNT(CountPoint::NUMBER_OF_RESEEKS_IN_ITERATION);

  // assume there is at least one parseable key for this user key
  ParsedInternalKey ikey;
  FindParseableKey(&ikey, kForward);

  if (ikey.type == kTypeDeletion || ikey.type == kTypeSingleDeletion) {
    valid_ = false;
    return false;
  }
  if (IsValueOrLargeType(ikey.type)) {
    assert(iter_->IsValuePinned());
    pinned_value_ = iter_->value();
    pinned_key_type_ = ikey.type;
    valid_ = true;
    return true;
  }

  se_assert(false);
  return true;
}

// Used in Next to change directions
// Go to next user key
// Don't use Seek(),
// because next user key will be very close
void DBIter::FindNextUserKey() {
  if (!iter_->Valid()) {
    return;
  }
  ParsedInternalKey ikey;
  FindParseableKey(&ikey, kForward);
  while (iter_->Valid() &&
         !user_comparator_->Equal(ikey.user_key, saved_key_.GetUserKey())) {
    iter_->Next();
    FindParseableKey(&ikey, kForward);
  }
}

// Go to previous user_key
void DBIter::FindPrevUserKey() {
  if (!iter_->Valid()) {
    return;
  }
  uint64_t num_skipped = 0;
  ParsedInternalKey ikey;
  FindParseableKey(&ikey, kReverse);
  int cmp = 0;
  while (iter_->Valid() &&
         ((cmp = user_comparator_->Compare(ikey.user_key,
                                           saved_key_.GetUserKey())) == 0 ||
          (cmp > 0 && ikey.sequence > sequence_))) {
    if (TooManyInternalKeysSkipped()) {
      return;
    }

    if (cmp == 0) {
      if (num_skipped >= max_skip_) {
        num_skipped = 0;
        IterKey last_key;
        last_key.SetInternalKey(ParsedInternalKey(
            saved_key_.GetUserKey(), kMaxSequenceNumber, kValueTypeForSeek));
        iter_->Seek(last_key.GetInternalKey());
        QUERY_COUNT(CountPoint::NUMBER_OF_RESEEKS_IN_ITERATION);
      } else {
        ++num_skipped;
      }
    }
    if (ikey.sequence > sequence_) {
      QUERY_COUNT(CountPoint::INTERNAL_UPD_SKIPPED);
    } else {
      QUERY_COUNT(CountPoint::INTERNAL_KEY_SKIPPED);
    }
    iter_->Prev();
    FindParseableKey(&ikey, kReverse);
  }
}

bool DBIter::TooManyInternalKeysSkipped(bool increment) {
  if ((max_skippable_internal_keys_ > 0) &&
      (num_internal_keys_skipped_ > max_skippable_internal_keys_)) {
    valid_ = false;
    status_ = Status::Incomplete("Too many internal keys skipped.");
    return true;
  } else if (increment) {
    num_internal_keys_skipped_++;
  }
  return false;
}

// Skip all unparseable keys
void DBIter::FindParseableKey(ParsedInternalKey* ikey, Direction direction) {
  while (iter_->Valid() && !ParseKey(ikey)) {
    if (direction == kReverse) {
      iter_->Prev();
    } else {
      iter_->Next();
    }
  }
}

void DBIter::Seek(const Slice& target) {
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_SEEK);
  ReleaseTempPinnedData();
  ResetInternalKeysSkippedCounter();
  saved_key_.Clear();
  saved_key_.SetInternalKey(target, sequence_);
  iter_->Seek(saved_key_.GetInternalKey());

  if (iter_->Valid()) {
    direction_ = kForward;
    ClearSavedValue();
    FindNextUserEntry(false /* not skipping */);
  } else {
    valid_ = false;
  }

}

void DBIter::SeekForPrev(const Slice& target) {
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_SEEK);
  ReleaseTempPinnedData();
  ResetInternalKeysSkippedCounter();
  saved_key_.Clear();
  // now saved_key is used to store internal key.
  saved_key_.SetInternalKey(target, 0 /* sequence_number */,
                            kValueTypeForSeekForPrev);
  iter_->SeekForPrev(saved_key_.GetInternalKey());

  if (iter_->Valid()) {
    direction_ = kReverse;
    ClearSavedValue();
    PrevInternal();
  } else {
    valid_ = false;
  }
}

void DBIter::SeekToFirst() {
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_SEEK);
  direction_ = kForward;
  ReleaseTempPinnedData();
  ResetInternalKeysSkippedCounter();
  ClearSavedValue();
  iter_->SeekToFirst();

  if (iter_->Valid()) {
    saved_key_.SetUserKey(
        ExtractUserKey(iter_->key()),
        !iter_->IsKeyPinned() || !pin_thru_lifetime_ /* copy */);
    FindNextUserEntry(false /* not skipping */);
  } else {
    valid_ = false;
  }
}

void DBIter::SeekToLast() {
  QUERY_TRACE_SCOPE(TracePoint::DB_ITER_SEEK);
  direction_ = kReverse;
  ReleaseTempPinnedData();
  ResetInternalKeysSkippedCounter();
  ClearSavedValue();

  iter_->SeekToLast();

  // When the iterate_upper_bound is set to a value,
  // it will seek to the last key before the
  // ReadOptions.iterate_upper_bound
  if (iter_->Valid() && iterate_upper_bound_ != nullptr) {
    SeekForPrev(*iterate_upper_bound_);
    if (!Valid()) {
      return;
    } else if (user_comparator_->Equal(*iterate_upper_bound_, key())) {
      Prev();
    }
  } else {
    PrevInternal();
  }
}

Iterator* NewDBIterator(Env* env,
                        const ReadOptions& read_options,
                        const ImmutableCFOptions& cf_options,
                        const Comparator* user_key_comparator,
                        InternalIterator* internal_iter,
                        const SequenceNumber& sequence,
                        uint64_t max_sequential_skip,
                        Arena* arena)
{
  DBIter* db_iter = nullptr;
  if (nullptr != arena) {
    db_iter = PLACEMENT_NEW(DBIter,
                            *arena,
                            env,
                            read_options,
                            cf_options,
                            user_key_comparator,
                            internal_iter,
                            sequence,
                            false,
                            max_sequential_skip);
  } else {
    db_iter = MOD_NEW_OBJECT(memory::ModId::kDbIter,
                             DBIter,
                             env,
                             read_options,
                             cf_options,
                             user_key_comparator,
                             internal_iter,
                             sequence,
                             false,
                             max_sequential_skip);
  }
  return db_iter;
}

Iterator* NewDBIterator(Env* env,
                        const ReadOptions& read_options,
                        const ImmutableCFOptions& cf_options,
                        const Comparator* user_key_comparator,
                        InternalIterator* internal_iter,
                        const SequenceNumber& sequence,
                        bool use_arena,
                        uint64_t max_sequential_skip,
                        Arena* arena)
{
  DBIter* db_iter = nullptr;
  if (nullptr != arena) {
    db_iter = PLACEMENT_NEW(DBIter,
                            *arena,
                            env,
                            read_options,
                            cf_options,
                            user_key_comparator,
                            internal_iter,
                            sequence,
                            use_arena,
                            max_sequential_skip);
  } else {
    db_iter = MOD_NEW_OBJECT(memory::ModId::kDbIter,
                             DBIter,
                             env,
                             read_options,
                             cf_options,
                             user_key_comparator,
                             internal_iter,
                             sequence,
                             false /**use_arena*/,
                             max_sequential_skip);
  }
  return db_iter;
}

ArenaWrappedDBIter::ArenaWrappedDBIter()
    : db_iter_(nullptr), arena_(Arena::kMinBlockSize, 0, memory::ModId::kDbIter) {}
ArenaWrappedDBIter::~ArenaWrappedDBIter() {
  if (nullptr != db_iter_) {
    db_iter_->~DBIter();
    db_iter_ = nullptr;
  }
}

// TODO(Zhao Dongsheng) : The interfaces SetDBIter and SetIterUnderDBIter are confused.
void ArenaWrappedDBIter::SetDBIter(DBIter* iter) { db_iter_ = iter; }

void ArenaWrappedDBIter::SetIterUnderDBIter(InternalIterator* iter) {
  static_cast<DBIter*>(db_iter_)->SetIter(iter);
}

inline bool ArenaWrappedDBIter::Valid() const { return db_iter_->Valid(); }
inline void ArenaWrappedDBIter::SeekToFirst() { db_iter_->SeekToFirst(); }
inline void ArenaWrappedDBIter::SeekToLast() { db_iter_->SeekToLast(); }
inline void ArenaWrappedDBIter::Seek(const Slice& target) {
  db_iter_->Seek(target);
}
inline void ArenaWrappedDBIter::SeekForPrev(const Slice& target) {
  db_iter_->SeekForPrev(target);
}
inline void ArenaWrappedDBIter::Next() { db_iter_->Next(); }

inline void ArenaWrappedDBIter::Prev() { db_iter_->Prev(); }
inline Slice ArenaWrappedDBIter::key() const { return db_iter_->key(); }
inline Slice ArenaWrappedDBIter::value() const { return db_iter_->value(); }
inline Status ArenaWrappedDBIter::status() const { return db_iter_->status(); }
void ArenaWrappedDBIter::RegisterCleanup(CleanupFunction function, void* arg1,
                                         void* arg2) {
  db_iter_->RegisterCleanup(function, arg1, arg2);
}

int ArenaWrappedDBIter::set_end_key(const Slice& end_key_slice)
{
  int ret = Status::kOk;
  if (UNLIKELY(nullptr == db_iter_)) {
    ret = Status::kNotInit;
  } else {
    ret = db_iter_->set_end_key(end_key_slice);
  }
  return ret;
}

SequenceNumber ArenaWrappedDBIter::key_seq() const { return db_iter_->key_seq(); }

Iterator::RecordStatus ArenaWrappedDBIter::key_status() const {
  return db_iter_->key_status();
}

bool ArenaWrappedDBIter::for_unique_check() const {
  return db_iter_->for_unique_check();
}

ArenaWrappedDBIter* NewArenaWrappedDbIterator(
    Env* env,
    const ReadOptions& read_options,
    const ImmutableCFOptions& cf_options,
    const Comparator* user_key_comparator,
    const SequenceNumber& sequence)
{
  const uint64_t MAX_SEQUENTIAL_SKIP = 100;
  ArenaWrappedDBIter* iter = MOD_NEW_OBJECT(memory::ModId::kDbIter, ArenaWrappedDBIter);
  Arena* arena = iter->GetArena();

  DBIter* db_iter = nullptr;
  if (!read_options.unique_check_) {
    auto mem = arena->AllocateAligned(sizeof(DBIter));
    db_iter = new (mem) DBIter(env,
                               read_options,
                               cf_options,
                               user_key_comparator,
                               nullptr,
                               sequence,
                               true,
                               MAX_SEQUENTIAL_SKIP);
  } else {
    ReadOptions ro = read_options;
    ro.skip_del_ = false;
    auto mem = arena->AllocateAligned(sizeof(UniqueCheckDBIterator));
    db_iter = new (mem) UniqueCheckDBIterator(env,
                                              ro,
                                              cf_options,
                                              user_key_comparator,
                                              nullptr,
                                              sequence,
                                              true,
                                              MAX_SEQUENTIAL_SKIP);
  }

  iter->SetDBIter(db_iter);

  return iter;
}

}
}  // namespace smartengine
