//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdint.h>
#include "smartengine/sst_dump_tool.h"

#include "table/table_builder.h"
#include "util/file_reader_writer.h"
#include "util/testharness.h"
#include "util/testutil.h"

using namespace smartengine;
using namespace common;
using namespace db;
using namespace table;
using namespace util;

namespace smartengine {
namespace tools {

const uint32_t optLength = 100;

namespace {
static std::string MakeKey(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "k_%04d", i);
  InternalKey key(std::string(buf), 0, ValueType::kTypeValue);
  return key.Encode().ToString();
}

static std::string MakeValue(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "v_%04d", i);
  InternalKey key(std::string(buf), 0, ValueType::kTypeValue);
  return key.Encode().ToString();
}

void createSST(const std::string& file_name,
               const BlockBasedTableOptions& table_options) {
  std::shared_ptr<TableFactory> tf;
  tf.reset(new BlockBasedTableFactory(table_options));

  unique_ptr<WritableFile> file;
  Env* env = Env::Default();
  EnvOptions env_options;
  ReadOptions read_options;
  Options opts;
  const ImmutableCFOptions imoptions(opts);
  InternalKeyComparator ikc(opts.comparator);
  unique_ptr<TableBuilder> tb;

  env->NewWritableFile(file_name, &file, env_options);
  opts.table_factory = tf;
  unique_ptr<WritableFileWriter> file_writer(
      new WritableFileWriter(std::move(file), EnvOptions()));
  std::string column_family_name;
  int unknown_level = -1;
  tb.reset(opts.table_factory->NewTableBuilder(
      TableBuilderOptions(imoptions, ikc,
                          CompressionType::kNoCompression, CompressionOptions(),
                          nullptr /* compression_dict */,
                          false /* skip_filters */, column_family_name,
                          unknown_level),
      TablePropertiesCollectorFactory::Context::kUnknownColumnFamily,
      file_writer.get()));

  // Populate slightly more than 1K keys
  uint32_t num_keys = 1024;
  for (uint32_t i = 0; i < num_keys; i++) {
    tb->Add(MakeKey(i), MakeValue(i));
  }
  tb->Finish();
  file_writer->Close();
}

void cleanup(const std::string& file_name) {
  Env* env = Env::Default();
  env->DeleteFile(file_name);
  std::string outfile_name = file_name.substr(0, file_name.length() - 4);
  outfile_name.append("_dump.txt");
  env->DeleteFile(outfile_name);
}
}  // namespace

// Test for sst dump tool "raw" mode
class SSTDumpToolTest : public testing::Test {
 public:
  BlockBasedTableOptions table_options_;

  SSTDumpToolTest() {}

  ~SSTDumpToolTest() {}
};

TEST_F(SSTDumpToolTest, EmptyFilter) {
  std::string file_name = "smartengine_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--command=raw");
  snprintf(usage[2], optLength, "--file=smartengine_sst_test.sst");

  tools::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, FilterBlock) {
  std::string file_name = "smartengine_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--command=raw");
  snprintf(usage[2], optLength, "--file=smartengine_sst_test.sst");

  tools::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, FullFilterBlock) {
  std::string file_name = "smartengine_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--command=raw");
  snprintf(usage[2], optLength, "--file=smartengine_sst_test.sst");

  tools::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, GetProperties) {
  std::string file_name = "smartengine_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--show_properties");
  snprintf(usage[2], optLength, "--file=smartengine_sst_test.sst");

  tools::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, CompressedSizes) {
  std::string file_name = "smartengine_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }

  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--show_compression_sizes");
  snprintf(usage[2], optLength, "--file=smartengine_sst_test.sst");
  tools::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}
}  // namespace tools
}  // namespace smartengine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}