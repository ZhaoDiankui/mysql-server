/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "db/version_set.h"
#include "options/cf_options.h"
#include "storage/extent_meta_manager.h"
#include "storage/extent_space_manager.h"
#include "storage/io_extent.h"
#include "storage/storage_logger.h"
#include "table/extent_writer.h"
#include "table/get_context.h"
#include "table/table.h"
#include "util/aio_wrapper.h"
#include "util/testharness.h"

static const std::string test_dir = smartengine::util::test::TmpDir() + "/in_mem_extent_table_test";

namespace smartengine
{
using namespace common;
using namespace db;
using namespace storage;
using namespace util;

namespace table
{

static std::string get_row(ExtentReader *extent_reader,
                           const std::string &key,
                           ReadOptions &reader_options,
                           const Comparator *comparator)
{
  int ret = Status::kOk;
  PinnableSlice value;
  GetContext get_context(comparator,
                         GetContext::kNotFound,
                         Slice(key),
                         &value,
                         nullptr,
                         nullptr);
  LookupKey lookup_key(key, kMaxSequenceNumber);
  ret = extent_reader->get(lookup_key.internal_key(), &get_context); 
  assert(Status::kOk == ret);
  return std::string(value.data(), value.size());
}

static void get_data_block(ExtentReader* extent_reader,
                           const Slice& key,
                           Slice &block) {
  int ret = Status::kOk;
  RowBlockIterator index_block_iter;
  ret = extent_reader->setup_index_block_iterator(&index_block_iter);
  assert(Status::kOk == ret);
  index_block_iter.Seek(key);
  assert(index_block_iter.Valid());

  int64_t pos = 0;
  BlockInfo block_info;
  Slice input = index_block_iter.value();
  ret = block_info.deserialize(input.data(), input.size(), pos);
  assert(Status::kOk == ret);

  char *buf = new char[block_info.get_handle().get_size()];
  ret = extent_reader->get_data_block(block_info.get_handle(), nullptr, buf, block);
  assert(Status::kOk == ret);
}

TEST(InMemExtent, sim) {
  // dirty preparation
  storage::ChangeInfo change_info;
  ExtentWriter writer;
  storage::LayerPosition output_layer_position;
  BlockBasedTableOptions table_options;
  Options options;
  options.table_factory.reset(NewExtentBasedTableFactory(table_options));
  const ImmutableCFOptions ioptions(options);
  const InternalKeyComparator internal_comparator(options.comparator);
  EnvOptions soptions;
  ReadOptions ro;
  Env* env = Env::Default();
  std::string dbname = test_dir;
  Status s;
  EXPECT_TRUE(s.ok()) << s.ToString();
  FileNumber file_number(2000);
  options.db_paths.emplace_back(dbname, 0);
  ImmutableDBOptions doption;
  VersionSet *vs = nullptr;
  vs = new VersionSet(dbname, &doption, soptions, nullptr, nullptr);

  StorageLogger::get_instance().init(env, dbname, soptions, doption, vs, 1 * 1024 * 1024 * 1024);
  ExtentMetaManager::get_instance().init();
  ExtentSpaceManager::get_instance().init(env, soptions, options);
  int ret = Status::kOk;
  ret = ExtentSpaceManager::get_instance().create_table_space(0);
  WriteExtentJobScheduler::get_instance().start(env, 1);
  ASSERT_EQ(Status::kOk, ret);
  StorageLogger::get_instance().begin(storage::SeEvent::FLUSH);
  schema::TableSchema table_schema;
  ExtentWriterArgs writer_args(std::string(),
                               0 /*table_space_id*/,
                               16 /*block_restart_interval*/,
                               storage::FILE_EXTENT_SPACE,
                               table_schema,
                               &internal_comparator,
                               output_layer_position,
                               nullptr /*block_cache*/,
                               nullptr /*row_cache*/,
                               kNoCompression,
                               &change_info);
  ret = writer.init(writer_args);
  ASSERT_EQ(Status::kOk, ret);

  // create an table/extent with one record
  InternalKey key("key", 0, kTypeValue);
  std::string value("val");
  ret = writer.append_row(key.Encode().ToString(), value);
  ASSERT_EQ(Status::kOk, ret);
  ret = writer.finish(nullptr /*extent_infos*/);
  ASSERT_EQ(Status::kOk, ret);

  //TODO(Zhao Dongsheng) : Temporarily hard-coded here
  //ExtentId eid(mtables.metas[0].extent_id_);
  ExtentId eid(1);

  common::Slice data_block1;
  {
    // method 1: read it normally
    ExtentReaderArgs extent_reader_args(eid, &internal_comparator, nullptr /*block_cache*/);
    ExtentReader extent_reader;
    ret = extent_reader.init(extent_reader_args);
    ASSERT_EQ(Status::kOk, ret);

    std::string val = get_row(&extent_reader, "key", ro, options.comparator);
    ASSERT_EQ(val, value);

    val = get_row(&extent_reader, "ke", ro, options.comparator);
    ASSERT_EQ(val.size(), 0); // Not exist

    val = get_row(&extent_reader, "keyx", ro, options.comparator);
    ASSERT_EQ(val.size(), 0);

    get_data_block(&extent_reader, key.Encode(), data_block1);
  }

  //common::Slice data_block2;
  //{
  //  // method 2: read it using the new added mem interface
  //  const int size = MAX_EXTENT_SIZE;
  //  FullPrefetchExtent *extent = MOD_NEW_OBJECT(memory::ModId::kDefaultMod, FullPrefetchExtent);
  //  s = ExtentSpaceManager::get_instance().get_readable_extent(eid, extent);
  //  EXPECT_TRUE(s.ok()) << s.ToString();

  //  // async read it
  //  extent->full_prefetch();

  //  // the mem interface
  //  TableReader *in_mem_table_reader = nullptr;
  //  TableReaderOptions reader_options(ioptions, internal_comparator, eid, false, -1);
  //  s = ioptions.table_factory->NewTableReader(reader_options,
  //                                             extent,
  //                                             MAX_EXTENT_SIZE,
  //                                             in_mem_table_reader);
  //  EXPECT_TRUE(s.ok()) << s.ToString();

  //  // verify
  //  std::string v =
  //      GetFromFile(in_mem_table_reader, "key", ro, options.comparator);
  //  ASSERT_EQ(v, value);
  //  v = GetFromFile(in_mem_table_reader, "ke", ro, options.comparator);
  //  ASSERT_EQ(v.size(), 0);  // not exist
  //  v = GetFromFile(in_mem_table_reader, "keyx", ro, options.comparator);
  //  ASSERT_EQ(v.size(), 0);  // not exist

  //  get_data_block(in_mem_table_reader, key.Encode(), block2, size2);
  //}
  //ASSERT_EQ(size1, size2);
  //ASSERT_EQ(memcmp(block1.get(), block2.get(), size1), 0);
  delete vs;
  StorageLogger::get_instance().destroy();
}

}  // table
}  // smartengine

int main(int argc, char** argv) {
  std::string log_path = smartengine::util::test::TmpDir() + "/in_mem_extent_table_test.log";
  smartengine::logger::Logger::get_log().init(log_path.c_str(), smartengine::logger::DEBUG_LEVEL);
  smartengine::util::test::remove_dir(test_dir.c_str());
  smartengine::util::Env::Default()->CreateDir(test_dir);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
