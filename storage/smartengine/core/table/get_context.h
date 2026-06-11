//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#pragma once
#include <string>
#include "env/env.h"
#include "util/types.h"
//#include "table/block.h"

namespace smartengine
{
namespace db
{
struct ParsedInternalKey;
class PinnedIteratorsManager;
} // namespace db
namespace util
{
class Comparator;
} // namespace util
namespace table
{
class GetContext
{
 public:
  enum GetState
  {
    kNotFound,
    kFound,
    kDeleted,
    kCorrupt
  };

  GetContext(const util::Comparator* ucmp,
             GetState init_state,
             const common::Slice& user_key,
             common::PinnableSlice* value,
             bool* value_found,
             common::SequenceNumber* seq);

  void MarkKeyMayExist();

  // Records this key, value, and any meta-data (such as sequence number and
  // state) into this GetContext.
  //
  // Returns True if more keys need to be read (due to merges) or
  //         False if the complete value has been found.
  bool SaveValue(const db::ParsedInternalKey& parsed_key,
                 const common::Slice& value,
                 common::Cleanable* value_pinner = nullptr);

  void SaveLargeValue(const common::Slice& value);

  GetState State() const { return state_; }

  // Do we need to fetch the common::SequenceNumber for this key?
  bool NeedToReadSequence() const { return (seq_ != nullptr); }

  common::PinnableSlice* get_pinnable_val() const { return pinnable_val_; }

  common::Slice get_user_key() const { return user_key_; }

 private:
  const util::Comparator* ucmp_;
  GetState state_;
  common::Slice user_key_;
  common::PinnableSlice* pinnable_val_;
  bool* value_found_;  // Is value set correctly? Used by KeyMayExist
  // If a key is found, seq_ will be set to the common::SequenceNumber of most
  // recent
  // write to the key or kMaxSequenceNumber if unknown
  common::SequenceNumber* seq_;
};

}  // namespace table
}  // namespace smartengine
