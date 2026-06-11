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

#pragma once
#include <stdio.h>
#include <string>
#include "db/db.h"
#include "storage/storage_common.h"
#include "table/table_properties.h"
#include "util/coding.h"
#include "util/serialization.h"

namespace smartengine
{
namespace storage
{
struct ChangeInfo;
}

namespace util
{
class Comparator;
}

namespace db
{

class InternalKey;

// Value types encoded as the last component of internal keys.
// DO NOT CHANGE THESE ENUM VALUES: they are embedded in the on-disk
// data structures.
// The highest bit of the value type needs to be reserved to SST tables
// for them to do more flexible encoding.
enum ValueType : unsigned char {
  kTypeDeletion = 0x0,
  kTypeValue = 0x1,
  kTypeMerge = 0x2, //deprecated
  kTypeLogData = 0x3,               // WAL only.
  kTypeColumnFamilyDeletion = 0x4,  // WAL only.
  kTypeColumnFamilyValue = 0x5,     // WAL only.
  kTypeColumnFamilyMerge = 0x6,     // WAL only.
  kTypeSingleDeletion = 0x7,
  kTypeColumnFamilySingleDeletion = 0x8,  // WAL only.
  kTypeBeginPrepareXID = 0x9,             // WAL only.
  kTypeEndPrepareXID = 0xA,               // WAL only.
  kTypeCommitXID = 0xB,                   // WAL only.
  kTypeRollbackXID = 0xC,                 // WAL only.
  kTypeNoop = 0xD,                        // WAL only.
  kTypeColumnFamilyRangeDeletion = 0xE,   // WAL only. deprecated
  kTypeRangeDeletion = 0xF,               // meta block. deprecated
  kTypeValueLarge = 0x10,                 // out-of-band large value
  kMaxValue = 0x7F                        // Not used for storing records.
};

// Defined in dbformat.cc
extern const ValueType kValueTypeForSeek;
extern const ValueType kValueTypeForSeekForPrev;

// Checks whether a type is an inline value type
// (i.e. a type used in memtable skiplist and sst file datablock).
inline bool IsValueType(ValueType t) {
  return t <= kTypeMerge || t == kTypeSingleDeletion || t == kTypeValueLarge;
}

inline bool IsValueOrLargeType(ValueType t) {
  return t == kTypeValue || t == kTypeValueLarge;
}

static const int MAX_SNAP = 32;

struct ParsedInternalKey {
  common::Slice user_key;
  common::SequenceNumber sequence;
  ValueType type;

  ParsedInternalKey() {}  // Intentionally left uninitialized (for speed)
  ParsedInternalKey(const common::Slice& u, const common::SequenceNumber& seq,
                    ValueType t)
      : user_key(u), sequence(seq), type(t) {}
  std::string DebugString(bool hex = false) const;
  ParsedInternalKey deep_copy(memory::SimpleAllocator& allocator) const {
    return ParsedInternalKey(user_key.deep_copy(allocator), sequence, type);
  }
  DECLARE_TO_STRING()
};

// Return the length of the encoding of "key".
inline size_t InternalKeyEncodingLength(const ParsedInternalKey& key) {
  return key.user_key.size() + 8;
}

// Pack a sequence number and a ValueType into a uint64_t
extern uint64_t PackSequenceAndType(uint64_t seq, ValueType t);

// Given the result of PackSequenceAndType, store the sequence number in *seq
// and the ValueType in *t.
extern void UnPackSequenceAndType(uint64_t packed, uint64_t* seq, ValueType* t);

// Append the serialization of "key" to *result.
extern void AppendInternalKey(std::string* result,
                              const ParsedInternalKey& key);

extern void AppendInternalKeyForNext(std::string* result, 
                                      const ParsedInternalKey& key);
// Serialized internal key consists of user key followed by footer.
// This function appends the footer to *result, assuming that *result already
// contains the user key at the end.
extern void AppendInternalKeyFooter(std::string* result,
                                    common::SequenceNumber s, ValueType t);

// Attempt to parse an internal key from "internal_key".  On success,
// stores the parsed data in "*result", and returns true.
//
// On error, returns false, leaves "*result" in an undefined state.
extern bool ParseInternalKey(const common::Slice& internal_key,
                             ParsedInternalKey* result);

extern int64_t internal_key_to_string(const common::Slice& internal_key, char *buf, int64_t buf_len);

// Returns the user key portion of an internal key.
inline common::Slice ExtractUserKey(const common::Slice& internal_key) {
  assert(internal_key.size() >= 8);
  return common::Slice(internal_key.data(), internal_key.size() - 8);
}

inline ValueType ExtractValueType(const common::Slice& internal_key) {
  assert(internal_key.size() >= 8);
  const size_t n = internal_key.size();
  uint64_t num = util::DecodeFixed64(internal_key.data() + n - 8);
  unsigned char c = num & 0xff;
  return static_cast<ValueType>(c);
}

inline common::SequenceNumber ExtractKeySeq(const common::Slice& internal_key) {
  assert(internal_key.size() >= 8);
  const size_t n = internal_key.size();
  uint64_t num = util::DecodeFixed64(internal_key.data() + n - 8);
  unsigned char c = num & 0xff;
  common::SequenceNumber sequence = num >> 8;
  return sequence;
}

// A comparator for internal keys that uses a specified comparator for
// the user key portion and breaks ties by decreasing sequence number.
class InternalKeyComparator : public util::Comparator {
 private:
  const util::Comparator* user_comparator_;
  //std::string name_;

 public:
  explicit InternalKeyComparator(const util::Comparator* c)
      : user_comparator_(c) {
        //name_ = "rocksdb.InternalKeyComparator";
      }
  InternalKeyComparator(const InternalKeyComparator &) = default;
  virtual ~InternalKeyComparator() override {}


  virtual const char* Name() const override;
  virtual int Compare(const common::Slice& a,
                      const common::Slice& b) const override;
  virtual void FindShortestSeparator(std::string* start,
                                     const common::Slice& limit) const override;
  virtual void FindShortSuccessor(std::string* key) const override;

  const util::Comparator* user_comparator() const { return user_comparator_; }

  int Compare(const InternalKey& a, const InternalKey& b) const;
  int Compare(const ParsedInternalKey& a, const ParsedInternalKey& b) const;
};

// Modules in this directory should keep internal keys wrapped inside
// the following class instead of plain strings so that we do not
// incorrectly use string comparisons instead of an InternalKeyComparator.
class InternalKey {
 private:
  std::string rep_;

 public:
  InternalKey() {}  // Leave rep_ as empty to indicate it is invalid
  InternalKey(const common::Slice& _user_key, common::SequenceNumber s,
              ValueType t) {
    AppendInternalKey(&rep_, ParsedInternalKey(_user_key, s, t));
  }

  // sets the internal key to be bigger or equal to all internal keys with this
  // user key
  void SetMaxPossibleForUserKey(const common::Slice& _user_key) {
    AppendInternalKey(&rep_, ParsedInternalKey(_user_key, common::kMaxSequenceNumber,
                                               kValueTypeForSeek));
  }

  // sets the internal key to be smaller or equal to all internal keys with this
  // user key
  void SetMinPossibleForUserKey(const common::Slice& _user_key) {
    AppendInternalKey(
        &rep_, ParsedInternalKey(_user_key, 0, static_cast<ValueType>(0)));
  }

  bool Valid() const {
    ParsedInternalKey parsed;
    return ParseInternalKey(common::Slice(rep_), &parsed);
  }

  void DecodeFrom(const common::Slice& s) { rep_.assign(s.data(), s.size()); }
  common::Slice Encode() const {
    assert(!rep_.empty());
    return rep_;
  }
  int64_t to_string(char* buf, const int64_t buf_len) const {
    return common::Slice(rep_).to_string(buf, buf_len);
  }

  common::Slice user_key() const { return ExtractUserKey(rep_); }
  size_t size() { return rep_.size(); }

  void Set(const common::Slice& _user_key, common::SequenceNumber s,
           ValueType t) {
    SetFrom(ParsedInternalKey(_user_key, s, t));
  }

  void SetFrom(const ParsedInternalKey& p) {
    rep_.clear();
    AppendInternalKey(&rep_, p);
  }

  void Clear() { rep_.clear(); }

  // The underlying representation.
  // Intended only to be used together with ConvertFromUserKey().
  std::string* rep() { return &rep_; }

  // Assuming that *rep() contains a user key, this method makes internal key
  // out of it in-place. This saves a memcpy compared to Set()/SetFrom().
  void ConvertFromUserKey(common::SequenceNumber s, ValueType t) {
    AppendInternalKeyFooter(&rep_, s, t);
  }

  std::string DebugString(bool hex = false) const;
  DECLARE_SERIALIZATION()
};

inline int InternalKeyComparator::Compare(const InternalKey& a,
                                          const InternalKey& b) const {
  return Compare(a.Encode(), b.Encode());
}

inline bool ParseInternalKey(const common::Slice& internal_key,
                             ParsedInternalKey* result) {
  const size_t n = internal_key.size();
  if (n < 8) return false;
  uint64_t num = util::DecodeFixed64(internal_key.data() + n - 8);
  unsigned char c = num & 0xff;
  result->sequence = num >> 8;
  result->type = static_cast<ValueType>(c);
  assert(result->type <= ValueType::kMaxValue);
  result->user_key = common::Slice(internal_key.data(), n - 8);
  return IsValueType(result->type);
}

// Update the sequence number in the internal key.
// Guarantees not to invalidate ikey.data().
inline void UpdateInternalKey(std::string* ikey, uint64_t seq, ValueType t) {
  size_t ikey_sz = ikey->size();
  assert(ikey_sz >= 8);
  uint64_t newval = (seq << 8) | t;

  // Note: Since C++11, strings are guaranteed to be stored contiguously and
  // string::operator[]() is guaranteed not to change ikey.data().
  util::EncodeFixed64(&(*ikey)[ikey_sz - 8], newval);
}

// Get the sequence number from the internal key
inline uint64_t GetInternalKeySeqno(const common::Slice& internal_key) {
  const size_t n = internal_key.size();
  assert(n >= 8);
  uint64_t num = util::DecodeFixed64(internal_key.data() + n - 8);
  return num >> 8;
}

// A helper class useful for DBImpl::Get()
class LookupKey {
 public:
  // Initialize *this for looking up user_key at a snapshot with
  // the specified sequence number.
  LookupKey(const common::Slice& _user_key, common::SequenceNumber sequence);

  ~LookupKey();

  // Return a key suitable for lookup in a MemTable.
  common::Slice memtable_key() const {
    return common::Slice(start_, static_cast<size_t>(end_ - start_));
  }

  // Return an internal key (suitable for passing to an internal iterator)
  common::Slice internal_key() const {
    return common::Slice(kstart_, static_cast<size_t>(end_ - kstart_));
  }

  // Return the user key
  common::Slice user_key() const {
    return common::Slice(kstart_, static_cast<size_t>(end_ - kstart_ - 8));
  }

  uint32_t get_bloom_hash_value() { return this->bloom_hash_value_; }

  bool is_bloom_hash_set() { return this->bloom_hash_set_; }

  void set_bloom_hash_value(uint32_t bloom_value) {
    this->bloom_hash_set_ = true;
    this->bloom_hash_value_ = bloom_value;
  }

 private:
  // We construct a char array of the form:
  //    klength  varint32               <-- start_
  //    userkey  char[klength]          <-- kstart_
  //    tag      uint64
  //                                    <-- end_
  // The array is a suitable MemTable key.
  // The suffix starting with "userkey" can be used as an InternalKey.
  const char* start_;
  const char* kstart_;
  const char* end_;
  char space_[200];  // Avoid allocation for short keys
  // hash value set flag
  bool bloom_hash_set_;
  // hash value flag
  uint32_t bloom_hash_value_;

  // No copying allowed
  LookupKey(const LookupKey&);
  void operator=(const LookupKey&);
};

inline LookupKey::~LookupKey() {
  if (start_ != space_) {
//    delete[] start_;
    memory::base_free((void *)start_);
  }
}

class IterKey {
 public:
  IterKey()
      : buf_(space_),
        buf_size_(sizeof(space_)),
        key_(buf_),
        key_size_(0),
        is_user_key_(true) {}

  ~IterKey() { ResetBuffer(); }

  common::Slice GetInternalKey() const {
    assert(!IsUserKey());
    return common::Slice(key_, key_size_);
  }

  common::Slice GetUserKey() const {
    if (IsUserKey()) {
      return common::Slice(key_, key_size_);
    } else {
      assert(key_size_ >= 8);
      return common::Slice(key_, key_size_ - 8);
    }
  }

  common::Slice GetUserKeyFromUserKey() const {
    return common::Slice(key_, key_size_);
  }

  size_t Size() const { return key_size_; }

  void Clear() { key_size_ = 0; }

  // Append "non_shared_data" to its back, from "shared_len"
  // This function is used in Block::Iter::ParseNextKey
  // shared_len: bytes in [0, shard_len-1] would be remained
  // non_shared_data: data to be append, its length must be >= non_shared_len
  void TrimAppend(const size_t shared_len, const char* non_shared_data,
                  const size_t non_shared_len) {
    assert(shared_len <= key_size_);
    size_t total_size = shared_len + non_shared_len;

    if (UNLIKELY(IsKeyPinned()) /* key is not in buf_ */) {
      // Copy the key from external memory to buf_ (copy shared_len bytes)
      EnlargeBufferIfNeeded(total_size);
      memcpy(buf_, key_, shared_len);
    } else if (UNLIKELY(total_size > buf_size_)) {
      // Need to allocate space, delete previous space
      char* p = new char[total_size];
      memcpy(p, key_, shared_len);

      if (buf_ != space_) {
        delete[] buf_;
      }

      buf_ = p;
      buf_size_ = total_size;
    }

    memcpy(buf_ + shared_len, non_shared_data, non_shared_len);
    key_ = buf_;
    key_size_ = total_size;
  }

  common::Slice SetUserKey(const common::Slice& key, bool copy = true) {
    is_user_key_ = true;
    return SetKeyImpl(key, copy);
  }

  common::Slice SetInternalKey(const common::Slice& key, bool copy = true) {
    is_user_key_ = false;
    return SetKeyImpl(key, copy);
  }

  // Copies the content of key, updates the reference to the user key in ikey
  // and returns a common::Slice referencing the new copy.
  common::Slice SetInternalKey(const common::Slice& key,
                               ParsedInternalKey* ikey) {
    size_t key_n = key.size();
    assert(key_n >= 8);
    SetInternalKey(key);
    ikey->user_key = common::Slice(key_, key_n - 8);
    return common::Slice(key_, key_n);
  }

  // Copy the key into IterKey own buf_
  void OwnKey() {
    assert(IsKeyPinned() == true);

    Reserve(key_size_);
    memcpy(buf_, key_, key_size_);
    key_ = buf_;
  }

  // Update the sequence number in the internal key.  Guarantees not to
  // invalidate slices to the key (and the user key).
  void UpdateInternalKey(uint64_t seq, ValueType t) {
    assert(!IsKeyPinned());
    assert(key_size_ >= 8);
    uint64_t newval = (seq << 8) | t;
    util::EncodeFixed64(&buf_[key_size_ - 8], newval);
  }

  bool IsKeyPinned() const { return (key_ != buf_); }

  void SetInternalKey(const common::Slice& key_prefix,
                      const common::Slice& user_key, common::SequenceNumber s,
                      ValueType value_type = kValueTypeForSeek) {
    size_t psize = key_prefix.size();
    size_t usize = user_key.size();
    EnlargeBufferIfNeeded(psize + usize + sizeof(uint64_t));
    if (psize > 0) {
      memcpy(buf_, key_prefix.data(), psize);
    }
    memcpy(buf_ + psize, user_key.data(), usize);
    util::EncodeFixed64(buf_ + usize + psize,
                        PackSequenceAndType(s, value_type));

    key_ = buf_;
    key_size_ = psize + usize + sizeof(uint64_t);
    is_user_key_ = false;
  }

  void SetInternalKey(const common::Slice& user_key, common::SequenceNumber s,
                      ValueType value_type = kValueTypeForSeek) {
    SetInternalKey(common::Slice(), user_key, s, value_type);
  }

  void Reserve(size_t size) {
    EnlargeBufferIfNeeded(size);
    key_size_ = size;
  }

  void SetInternalKey(const ParsedInternalKey& parsed_key) {
    SetInternalKey(common::Slice(), parsed_key);
  }

  void SetInternalKey(const common::Slice& key_prefix,
                      const ParsedInternalKey& parsed_key_suffix) {
    SetInternalKey(key_prefix, parsed_key_suffix.user_key,
                   parsed_key_suffix.sequence, parsed_key_suffix.type);
  }

  void EncodeLengthPrefixedKey(const common::Slice& key) {
    auto size = key.size();
    EnlargeBufferIfNeeded(size + static_cast<size_t>(util::VarintLength(size)));
    char* ptr = util::EncodeVarint32(buf_, static_cast<uint32_t>(size));
    memcpy(ptr, key.data(), size);
    key_ = buf_;
    is_user_key_ = true;
  }

  bool IsUserKey() const { return is_user_key_; }

 private:
  char* buf_;
  size_t buf_size_;
  const char* key_;
  size_t key_size_;
  char space_[32];  // Avoid allocation for short keys
  bool is_user_key_;

  common::Slice SetKeyImpl(const common::Slice& key, bool copy) {
    size_t size = key.size();
    if (LIKELY(copy)) {
      // Copy key to buf_
      EnlargeBufferIfNeeded(size);
      memcpy(buf_, key.data(), size);
      key_ = buf_;
    } else {
      // Update key_ to point to external memory
      key_ = key.data();
    }
    key_size_ = size;
    return common::Slice(key_, key_size_);
  }

  void ResetBuffer() {
    if (buf_ != space_) {
      delete[] buf_;
      buf_ = space_;
    }
    buf_size_ = sizeof(space_);
    key_size_ = 0;
  }

  // Enlarge the buffer size if needed based on key_size.
  // By default, static allocated buffer is used. Once there is a key
  // larger than the static allocated buffer, another buffer is dynamically
  // allocated, until a larger key buffer is requested. In that case, we
  // reallocate buffer and delete the old one.
  void EnlargeBufferIfNeeded(size_t key_size) {
    // If size is smaller than buffer size, continue using current buffer,
    // or the static allocated one, as default
    if (UNLIKELY(key_size > buf_size_)) {
      // Need to enlarge the buffer.
      ResetBuffer();
      buf_ = new char[key_size];
      buf_size_ = key_size;
    }
  }

  // No copying allowed
  IterKey(const IterKey&) = delete;
  void operator=(const IterKey&) = delete;
};

// Read the key of a record from a write batch.
// if this record represent the default column family then cf_record
// must be passed as false, otherwise it must be passed as true.
extern bool ReadKeyFromWriteBatchEntry(common::Slice* input, common::Slice* key,
                                       bool cf_record);

// Read record from a write batch piece from input.
// tag, column_family, key, value and blob are return values. Callers own the
// common::Slice they point to.
// Tag is defined as ValueType.
// input will be advanced to after the record.
extern common::Status ReadRecordFromWriteBatch(common::Slice* input, char* tag,
                                               uint32_t* column_family,
                                               common::Slice* key,
                                               common::Slice* value,
                                               common::Slice* blob,
                                               common::Slice* xid,
                                               common::SequenceNumber* prepare_seq = nullptr);

//TODO: Zhao Dongsheng. Temporatily move FileDescriptor, FileMetaData, and MiniTables to
//here, they will be placed in more approriate file in the future.
//
struct FileMetaData {
  int refs;
  storage::ExtentId extent_id_;
  int64_t data_size_;
  db::InternalKey smallest;  // Smallest internal key served by table
  db::InternalKey largest;   // Largest internal key served by table
  bool being_compacted;  // Is this file undergoing compaction?
  common::SequenceNumber smallest_seqno;  // The smallest seqno in this file
  common::SequenceNumber largest_seqno;   // The largest seqno in this file

  // Stats for compensating deletion entries during compaction

  // These values can mutate, but they can only be read or written from
  // single-threaded LogAndApply thread
  uint64_t num_entries;       // the number of entries.
  uint64_t num_deletions;     // the number of deletion entries.
  uint64_t raw_key_size;      // total uncompressed key size.
  uint64_t raw_value_size;    // total uncompressed value size.
  bool init_stats_from_file;  // true if the data-entry stats of this file
                              // has initialized from file.

  bool marked_for_compaction;  // True if client asked us nicely to compact this
                               // file.

  FileMetaData()
      : refs(0),
        extent_id_(),
        data_size_(0),
        smallest(),
        largest(),
        being_compacted(false),
        smallest_seqno(common::kMaxSequenceNumber),
        largest_seqno(0),
        num_entries(0),
        num_deletions(0),
        raw_key_size(0),
        raw_value_size(0),
        init_stats_from_file(false),
        marked_for_compaction(false) {}

  // REQUIRED: Keys must be given to the function in sorted order (it expects
  // the last key to be the largest).
  void UpdateBoundaries(const common::Slice& key,
                        common::SequenceNumber seqno) {
    if (smallest.size() == 0) {
      smallest.DecodeFrom(key);
    }
    largest.DecodeFrom(key);
    smallest_seqno = std::min(smallest_seqno, seqno);
    largest_seqno = std::max(largest_seqno, seqno);
  }

  DECLARE_TO_STRING()
};

struct MiniTables {
  util::Env::IOPriority io_priority;
  std::vector<FileMetaData> metas;
  std::vector<table::TableProperties> props;
  storage::ChangeInfo *change_info_ = nullptr;
  int level = 0;
  int64_t table_space_id_ = -1;
};

}
}  // namespace smartengine
