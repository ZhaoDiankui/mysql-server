//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//

#include "memtable/inlineskiplist.h"
#include "memtable/memtable.h"
#include "memtable/memtablerep.h"
#include "util/arena.h"

namespace smartengine {
using namespace common;
using namespace util;
using namespace db;

namespace memtable {
namespace {
class SkipListRep : public MemTableRep {
  InlineSkipList<const MemTableRep::KeyComparator&> skip_list_;
  const MemTableRep::KeyComparator& cmp_;

  friend class LookaheadIterator;

 public:
  explicit SkipListRep(const MemTableRep::KeyComparator& compare,
                       MemTableAllocator* allocator)
      : MemTableRep(allocator),
        skip_list_(compare, allocator),
        cmp_(compare)
  {}

  virtual KeyHandle Allocate(const size_t len, char** buf) override {
    *buf = skip_list_.AllocateKey(len);
    return static_cast<KeyHandle>(*buf);
  }

  // Insert key into the list.
  // REQUIRES: nothing that compares equal to key is currently in the list.
  virtual void Insert(KeyHandle handle) override {
    skip_list_.Insert(static_cast<char*>(handle));
  }

  virtual void InsertWithHint(KeyHandle handle, void** hint) override {
    skip_list_.InsertWithHint(static_cast<char*>(handle), hint);
  }

  virtual void InsertConcurrently(KeyHandle handle) override {
    skip_list_.InsertConcurrently(static_cast<char*>(handle));
  }

  // Returns true iff an entry that compares equal to key is in the list.
  virtual bool Contains(const char* key) const override {
    return skip_list_.Contains(key);
  }

  virtual size_t ApproximateMemoryUsage() override {
    // All memory is allocated through allocator; nothing to report here
    return 0;
  }

  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg,
                                         const char* entry)) override {
    SkipListRep::Iterator iter(&skip_list_);
    Slice dummy_slice;
    for (iter.Seek(dummy_slice, k.memtable_key().data());
         iter.Valid() && callback_func(callback_args, iter.key());
         iter.Next()) {
    }
  }

  // obsolete
  //  uint64_t ApproximateNumEntries(const Slice& start_ikey,
  //                                 const Slice& end_ikey) override {
  //    std::string tmp;
  //    uint64_t start_count =
  //        skip_list_.EstimateCount(EncodeKey(&tmp, start_ikey));
  //    uint64_t end_count = skip_list_.EstimateCount(EncodeKey(&tmp,
  //    end_ikey));
  //    return (end_count >= start_count) ? (end_count - start_count) : 0;
  //  }
  //
  uint64_t ApproximateNumEntries(const Slice& start_ikey,
                                 const Slice& end_ikey) override {
    std::string tmp1;
    std::string tmp2;
    uint64_t count = skip_list_.EstimateCount(EncodeKey(&tmp1, start_ikey),
                                              EncodeKey(&tmp2, end_ikey));
    return count > 0 ? count : 0;
  }

  virtual ~SkipListRep() override {}

  // Iteration over the contents of a skip list
  class Iterator : public MemTableRep::Iterator {
    InlineSkipList<const MemTableRep::KeyComparator&>::Iterator iter_;

   public:
    // Initialize an iterator over the specified list.
    // The returned iterator is not valid.
    explicit Iterator(
        const InlineSkipList<const MemTableRep::KeyComparator&>* list)
        : iter_(list) {}

    virtual ~Iterator() override {}

    // Returns true iff the iterator is positioned at a valid node.
    virtual bool Valid() const override { return iter_.Valid(); }

    // Returns the key at the current position.
    // REQUIRES: Valid()
    virtual const char* key() const override { return iter_.key(); }

    // Advances to the next position.
    // REQUIRES: Valid()
    virtual void Next() override { iter_.Next(); }

    // Advances to the previous position.
    // REQUIRES: Valid()
    virtual void Prev() override { iter_.Prev(); }

    // Advance to the first entry with a key >= target
    virtual void Seek(const Slice& user_key,
                      const char* memtable_key) override {
      if (memtable_key != nullptr) {
        iter_.Seek(memtable_key);
      } else {
        iter_.Seek(user_key);
      }
    }

    // Retreat to the last entry with a key <= target
    virtual void SeekForPrev(const Slice& user_key,
                             const char* memtable_key) override {
      if (memtable_key != nullptr) {
        iter_.SeekForPrev(memtable_key);
      } else {
        iter_.SeekForPrev(EncodeKey(&tmp_, user_key));
      }
    }

    // Position at the first entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    virtual void SeekToFirst() override { iter_.SeekToFirst(); }

    // Position at the last entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    virtual void SeekToLast() override { iter_.SeekToLast(); }

   protected:
    std::string tmp_;  // For passing to EncodeKey
  };


  virtual MemTableRep::Iterator* GetIterator(Arena* arena = nullptr) override {
    void* mem = arena ? arena->AllocateAligned(sizeof(SkipListRep::Iterator))
                      : operator new(sizeof(SkipListRep::Iterator));
    return new (mem) SkipListRep::Iterator(&skip_list_);
  }
};
}

MemTableRep* SkipListFactory::CreateMemTableRep(const MemTableRep::KeyComparator& compare,
                                                memtable::MemTableAllocator* allocator)
{
  return new memtable::SkipListRep(compare, allocator);
}

}  // namespace memtable
}  // namespace smartengine
