// Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef SMARTENGINE_FLUSH_ITERATOR_H_
#define SMARTENGINE_FLUSH_ITERATOR_H_

#include <vector>

#include "compact/compaction_stats.h"
#include "db/dbformat.h"

namespace smartengine
{
namespace storage
{
class FlushIterator {
 public:
  FlushIterator();
  ~FlushIterator();

  int init(table::InternalIterator *iter,
           const util::Comparator *cmp,
           const std::vector<common::SequenceNumber> *snapshots,
           const common::SequenceNumber earliest_write_conflict_snapshot,
           const bool background_disable_merge);
  int SeekToFirst();
  int Next();
  const common::Slice& key() const { return key_; }
  const common::Slice& value() const { return value_; }
  int inner_ret() const { return inner_ret_; }
  bool Valid() const { return valid_; }

 private:
  // Processes the input stream to find the next output
  void inner_next();
  void deal_single_deletion(common::SequenceNumber prev_snapshot);

  // Given a sequence number, return the sequence number of the
  // earliest snapshot that this sequence number is visible in.
  // The snapshots themselves are arranged in ascending order of
  // sequence numbers.
  // Employ a sequential search because the total number of
  // snapshots are typically small.
  common::SequenceNumber findEarliestVisibleSnapshot(common::SequenceNumber in,
                                                     common::SequenceNumber* prev_snapshot);

 private:
  bool is_inited_;
  int inner_ret_;
  // Points to a copy of the current compaction iterator output (current_key_)
  // if valid_.
  bool valid_;
  table::InternalIterator* input_;
  const util::Comparator* cmp_;
  const std::vector<common::SequenceNumber>* snapshots_;
  common::SequenceNumber earliest_write_conflict_snapshot_;
  common::Slice key_;
  // Points to the value in the underlying iterator that corresponds to the
  // current output.
  common::Slice value_;
  // Stores the user key, sequence number and type of the current compaction
  // iterator output (or current key in the underlying iterator during
  // NextFromInput()).
  db::ParsedInternalKey ikey_;
  // Stores whether ikey_.user_key is valid. If set to false, the user key is
  // not compared against the current key in the underlying iterator.
  bool has_current_user_key_;
  // If false, the iterator holds a copy of the current compaction iterator
  // output (or current key in the underlying iterator during NextFromInput()).
  bool at_next_;
  db::IterKey current_key_;
  common::Slice current_user_key_;
  common::SequenceNumber current_user_key_sequence_;
  common::SequenceNumber current_user_key_snapshot_;
  // True if the iterator has already returned a record for the current key.
  bool has_outputted_key_;
  // truncated the value of the next key and output it without applying any
  // compaction rules.  This is used for outputting a put after a single delete.
  bool clear_and_output_next_key_;
  // PinnedIteratorsManager used to pin input_ Iterator blocks while reading
  // merge operands and then releasing them after consuming them.
  //db::PinnedIteratorsManager pinned_iters_mgr_;
  CompactionIterationStats iter_stats_;
  bool background_disable_merge_;
};

}  // namespace storage
}  // namespace smartengine

#endif // SMARTENGINE_FLUSH_ITERATOR_H_
