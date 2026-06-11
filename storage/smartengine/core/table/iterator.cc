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

#include "table/iterator.h"

#include "table/iterator_wrapper.h"
#include "db/dbformat.h"

namespace smartengine {
using namespace table;
using namespace db;
using namespace util;

namespace common {

Cleanable::Cleanable() {
  cleanup_.function = nullptr;
  cleanup_.next = nullptr;
}

Cleanable::~Cleanable() { DoCleanup(); }

// If the entire linked list was on heap we could have simply add attach one
// link list to another. However the head is an embeded object to avoid the cost
// of creating objects for most of the use cases when the Cleanable has only one
// Cleanup to do. We could put evernything on heap if benchmarks show no
// negative impact on performance.
// Also we need to iterate on the linked list since there is no pointer to the
// tail. We can add the tail pointer but maintainin it might negatively impact
// the perforamnce for the common case of one cleanup where tail pointer is not
// needed. Again benchmarks could clarify that.
// Even without a tail pointer we could iterate on the list, find the tail, and
// have only that node updated without the need to insert the Cleanups one by
// one. This however would be redundant when the source Cleanable has one or a
// few Cleanups which is the case most of the time.
// TODO(myabandeh): if the list is too long we should maintain a tail pointer
// and have the entire list (minus the head that has to be inserted separately)
// merged with the target linked list at once.
void Cleanable::DelegateCleanupsTo(Cleanable* other) {
  assert(other != nullptr);
  if (cleanup_.function == nullptr) {
    return;
  }
  Cleanup* c = &cleanup_;
  other->RegisterCleanup(c->function, c->arg1, c->arg2);
  c = c->next;
  while (c != nullptr) {
    Cleanup* next = c->next;
    other->RegisterCleanup(c);
    c = next;
  }
  cleanup_.function = nullptr;
  cleanup_.next = nullptr;
}

void Cleanable::RegisterCleanup(Cleanable::Cleanup* c) {
  assert(c != nullptr);
  if (cleanup_.function == nullptr) {
    cleanup_.function = c->function;
    cleanup_.arg1 = c->arg1;
    cleanup_.arg2 = c->arg2;
    delete c;
  } else {
    c->next = cleanup_.next;
    cleanup_.next = c;
  }
}

void Cleanable::RegisterCleanup(CleanupFunction func, void* arg1, void* arg2) {
  assert(func != nullptr);
  Cleanup* c;
  if (cleanup_.function == nullptr) {
    c = &cleanup_;
  } else {
    c = new Cleanup;
    c->next = cleanup_.next;
    cleanup_.next = c;
  }
  c->function = func;
  c->arg1 = arg1;
  c->arg2 = arg2;
}
}  // namespace common

// TODO(Zhao Dongsheng): inappropriate code location.
namespace db {
common::SequenceNumber Iterator::key_seq() const
{
  return common::kMaxSequenceNumber;
}
}  // namespace db

namespace {
class EmptyIterator : public Iterator {
 public:
  explicit EmptyIterator(const common::Status& s) : status_(s) {}
  virtual bool Valid() const override { return false; }
  virtual void Seek(const common::Slice& target) override {}
  virtual void SeekForPrev(const common::Slice& target) override {}
  virtual void SeekToFirst() override {}
  virtual void SeekToLast() override {}
  virtual void Next() override { assert(false); }
  virtual void Prev() override { assert(false); }
  common::Slice key() const override {
    assert(false);
    return common::Slice();
  }
  common::Slice value() const override {
    assert(false);
    return common::Slice();
  }
  virtual common::Status status() const override { return status_; }

 private:
  common::Status status_;
};

class EmptyInternalIterator : public InternalIterator {
 public:
  explicit EmptyInternalIterator(const common::Status& s) : status_(s) {}
  virtual bool Valid() const override { return false; }
  virtual void Seek(const common::Slice& target) override {}
  virtual void SeekForPrev(const common::Slice& target) override {}
  virtual void SeekToFirst() override {}
  virtual void SeekToLast() override {}
  virtual void Next() override { assert(false); }
  virtual void Prev() override { assert(false); }
  common::Slice key() const override {
    assert(false);
    return common::Slice();
  }
  common::Slice value() const override {
    assert(false);
    return common::Slice();
  }
  virtual common::Status status() const override { return status_; }

 private:
  common::Status status_;
};
}  // namespace

namespace db {
Iterator* NewEmptyIterator() { return new EmptyIterator(common::Status::OK()); }

Iterator* NewErrorIterator(const common::Status& status) {
  return new EmptyIterator(status);
}
}

namespace table {
InternalIterator* NewEmptyInternalIterator() {
//  return new EmptyInternalIterator(Status::OK());
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, EmptyInternalIterator, common::Status::OK());
}

InternalIterator* NewEmptyInternalIterator(memory::SimpleAllocator* arena) {
  if (arena == nullptr) {
    return NewEmptyInternalIterator();
  } else {
    auto mem = arena->alloc(sizeof(EmptyIterator));
    return new (mem) EmptyInternalIterator(common::Status::OK());
  }
}

InternalIterator* NewErrorInternalIterator(const common::Status& status) {
//  return new EmptyInternalIterator(status);
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, EmptyInternalIterator, status);
}

InternalIterator* NewErrorInternalIterator(const common::Status& status, memory::SimpleAllocator* arena) {
  if (arena == nullptr) {
    return NewErrorInternalIterator(status);
  } else {
    auto mem = arena->alloc(sizeof(EmptyIterator));
    return new (mem) EmptyInternalIterator(status);
  }
}
}  // namespace table

}  // namespace smartengine
