//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/testutil.h"

#include <cctype>
#include <climits>
#include <sstream>

#include "table/table.h"
#include "util/concurrent_direct_file_writer.h"
#include "util/file_reader_writer.h"

namespace smartengine
{
using namespace common;
using namespace db;
using namespace port;
using namespace storage;
using namespace table;

namespace util
{
namespace test
{
Slice RandomString(Random* rnd, int len, std::string* dst) {
  dst->resize(len);
  for (int i = 0; i < len; i++) {
    (*dst)[i] = static_cast<char>(' ' + rnd->Uniform(95));  // ' ' .. '~'
  }
  return Slice(*dst);
}

extern std::string RandomHumanReadableString(Random* rnd, int len) {
  std::string ret;
  ret.resize(len);
  for (int i = 0; i < len; ++i) {
    ret[i] = static_cast<char>('a' + rnd->Uniform(26));
  }
  return ret;
}

std::string RandomKey(Random* rnd, int len, RandomKeyType type) {
  // Make sure to generate a wide variety of characters so we
  // test the boundary conditions for short-key optimizations.
  static const char kTestChars[] = {'\0', '\1', 'a',    'b',    'c',
                                    'd',  'e',  '\xfd', '\xfe', '\xff'};
  std::string result;
  for (int i = 0; i < len; i++) {
    std::size_t indx = 0;
    switch (type) {
      case RandomKeyType::RANDOM:
        indx = rnd->Uniform(sizeof(kTestChars));
        break;
      case RandomKeyType::LARGEST:
        indx = sizeof(kTestChars) - 1;
        break;
      case RandomKeyType::MIDDLE:
        indx = sizeof(kTestChars) / 2;
        break;
      case RandomKeyType::SMALLEST:
        indx = 0;
        break;
    }
    result += kTestChars[indx];
  }
  return result;
}

extern Slice CompressibleString(Random* rnd, double compressed_fraction,
                                int len, std::string* dst) {
  int raw = static_cast<int>(len * compressed_fraction);
  if (raw < 1) raw = 1;
  std::string raw_data;
  RandomString(rnd, raw, &raw_data);

  // Duplicate the random data until we have filled "len" bytes
  dst->clear();
  while (dst->size() < (unsigned int)len) {
    dst->append(raw_data);
  }
  dst->resize(len);
  return Slice(*dst);
}

namespace {
class Uint64ComparatorImpl : public Comparator {
 public:
  Uint64ComparatorImpl() {}

  virtual const char* Name() const override {
    return "smartengine.Uint64Comparator";
  }

  virtual int Compare(const Slice& a, const Slice& b) const override {
    // assert(a.size() == sizeof(uint64_t) && b.size() == sizeof(uint64_t));
    uint64_t zero = 0;
    const uint64_t* left = nullptr;
    if (0 == a.size()) {
      left = &(zero);
    } else {
      left = reinterpret_cast<const uint64_t*>(a.data());
    }
    const uint64_t* right = nullptr;
    if (0 == b.size()) {
      right = &(zero);
    } else {
      right = reinterpret_cast<const uint64_t*>(b.data());
    }
    if (*left == *right) {
      return 0;
    } else if (*left < *right) {
      return -1;
    } else {
      return 1;
    }
  }

  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const override {
    return;
  }

  virtual void FindShortSuccessor(std::string* key) const override { return; }
};
}  // namespace

static port::OnceType once;
static const Comparator* uint64comp;

static void InitModule() { uint64comp = new Uint64ComparatorImpl; }

const Comparator* Uint64Comparator() {
  port::InitOnce(&once, InitModule);
  return uint64comp;
}

WritableFileWriter* GetWritableFileWriter(WritableFile* wf) {
//  unique_ptr<WritableFile> file(wf);
//  return new WritableFileWriter(wf, EnvOptions());
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, WritableFileWriter, wf, EnvOptions());
}

using smartengine::util::ConcurrentDirectFileWriter;

ConcurrentDirectFileWriter* GetConcurrentDirectFileWriter(WritableFile* wf) {
//  unique_ptr<WritableFile> file(wf);
//  return new ConcurrentDirectFileWriter(wf, EnvOptions());
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, ConcurrentDirectFileWriter, wf, EnvOptions());
}

RandomAccessFileReader* GetRandomAccessFileReader(RandomAccessFile* raf) {
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, RandomAccessFileReader, raf, false /*use_allocator*/);
}

SequentialFileReader* GetSequentialFileReader(SequentialFile* se) {
//  unique_ptr<SequentialFile> file(se);
//  return new SequentialFileReader(se);
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, SequentialFileReader, se);
}

void CorruptKeyType(InternalKey* ikey) {
  std::string keystr = ikey->Encode().ToString();
  keystr[keystr.size() - 8] = kTypeLogData;
  ikey->DecodeFrom(Slice(keystr.data(), keystr.size()));
}

std::string KeyStr(const std::string& user_key, const SequenceNumber& seq,
                   const ValueType& t, bool corrupt) {
  InternalKey k(user_key, seq, t);
  if (corrupt) {
    CorruptKeyType(&k);
  }
  return k.Encode().ToString();
}

std::string RandomName(Random* rnd, const size_t len) {
  std::stringstream ss;
  for (size_t i = 0; i < len; ++i) {
    ss << static_cast<char>(rnd->Uniform(26) + 'a');
  }
  return ss.str();
}

CompressionType RandomCompressionType(Random* rnd) {
  return static_cast<CompressionType>(rnd->Uniform(6));
}

BlockBasedTableOptions RandomBlockBasedTableOptions(Random* rnd) {
  BlockBasedTableOptions opt;
  opt.cache_index_and_filter_blocks = rnd->Uniform(2);
  opt.pin_l0_filter_and_index_blocks_in_cache = rnd->Uniform(2);
  opt.checksum = static_cast<ChecksumType>(rnd->Uniform(3));
  opt.block_size = rnd->Uniform(10000000);
  opt.block_size_deviation = rnd->Uniform(100);
  opt.block_restart_interval = rnd->Uniform(100);
  opt.index_block_restart_interval = rnd->Uniform(100);

  return opt;
}

void RandomInitDBOptions(DBOptions* db_opt, Random* rnd) {
  // boolean options
  db_opt->use_direct_reads = rnd->Uniform(2);
  db_opt->enable_thread_tracking = rnd->Uniform(2);
  db_opt->avoid_flush_during_recovery = rnd->Uniform(2);
  db_opt->avoid_flush_during_shutdown = rnd->Uniform(2);

  // int options
  db_opt->max_background_compactions = rnd->Uniform(100);
  db_opt->max_background_flushes = rnd->Uniform(100);
  db_opt->table_cache_numshardbits = rnd->Uniform(100);

  // size_t options
  db_opt->db_total_write_buffer_size = rnd->Uniform(10000);

  // std::string options
  db_opt->wal_dir = "path/to/wal_dir";

  // uint64_t options
  static const uint64_t uint_max = static_cast<uint64_t>(UINT_MAX);
  db_opt->bytes_per_sync = uint_max + rnd->Uniform(100000);
  db_opt->delete_obsolete_files_period_micros = uint_max + rnd->Uniform(100000);
  db_opt->max_total_wal_size = uint_max + rnd->Uniform(100000);
  db_opt->wal_bytes_per_sync = uint_max + rnd->Uniform(100000);

  // unsigned int options
  db_opt->stats_dump_period_sec = rnd->Uniform(100000);
}

void RandomInitCFOptions(ColumnFamilyOptions* cf_opt, Random* rnd) {
  // boolean options
  cf_opt->disable_auto_compactions = rnd->Uniform(2);
  cf_opt->level_compaction_dynamic_level_bytes = rnd->Uniform(2);

  // double options

  // int options
  cf_opt->level0_file_num_compaction_trigger = rnd->Uniform(100);
  cf_opt->level0_layer_num_compaction_trigger = rnd->Uniform(100);
  cf_opt->level1_extents_major_compaction_trigger = rnd->Uniform(100);
  cf_opt->level2_usage_percent = rnd->Uniform(100);
  cf_opt->max_write_buffer_number_to_maintain = rnd->Uniform(100);
  cf_opt->min_write_buffer_number_to_merge = rnd->Uniform(100);

  // size_t options
  cf_opt->write_buffer_size = rnd->Uniform(10000);

  // uint64_t options
  static const uint64_t uint_max = static_cast<uint64_t>(UINT_MAX);
}

Status DestroyDir(Env* env, const std::string& dir) {
  Status s;
  if (env->FileExists(dir).IsNotFound()) {
    return s;
  }
  std::vector<std::string> files_in_dir;
  s = env->GetChildren(dir, &files_in_dir);
  if (s.ok()) {
    for (auto& file_in_dir : files_in_dir) {
      if (file_in_dir == "." || file_in_dir == "..") {
        continue;
      }
      s = env->DeleteFile(dir + "/" + file_in_dir);
      if (!s.ok()) {
        break;
      }
    }
  }

  if (s.ok()) {
    s = env->DeleteDir(dir);
  }
  return s;
}

RandomInt64Generator::RandomInt64Generator(const int64_t lower_bound, const int64_t upper_bound)
    : rd_(),
      generator_(rd_()),
      distribution_(lower_bound, upper_bound)
{
  se_assert(upper_bound >= lower_bound);
}

int64_t RandomInt64Generator::generate() { return distribution_(generator_); }

std::string RandomStringGenerator::generate(int64_t size)
{
  // use current time as seed
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  std::mt19937_64 generator(seed);
  std::uniform_int_distribution<int> distribution(32, 126); // ASCII visible range

  // generate
  std::string result;
  result.reserve(size);
  for (int64_t i = 0; i < size; ++i) {
      result.push_back(static_cast<char>(distribution(generator)));
  }

  return result;
}

}  // namespace test
}  // namespace util
}  // namespace smartengine
