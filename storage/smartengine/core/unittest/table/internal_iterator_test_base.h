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
#pragma once

#include "compact/compaction.h"
#include "db/db_impl.h"
#include "db/version_set.h"
#include "db/dbformat.h"
#include "options/options_helper.h"
#include "storage/extent_meta_manager.h"
#include "storage/extent_space_manager.h"
#include "storage/storage_logger.h"
#include "storage/storage_manager.h"
#include "table/extent_writer.h"
#include "table/extent_table_factory.h"
#include "util/file_reader_writer.h"
#include "util/testharness.h"
#include <cstddef>
#define private public
#define protected public

namespace smartengine
{
using namespace cache;
using namespace storage;
using namespace table;

namespace table
{

//class TESTFlushBlockPolicy : public FlushBlockPolicy
//{
//public:
//  TESTFlushBlockPolicy() : need_flush_(false)
//  {}
//  virtual bool Update(const common::Slice& key, const common::Slice& value) override
//  {
//    bool need_flush = need_flush_;
//    need_flush_ = false;
//    return need_flush;
//  }
//  void set_need_flush()
//  {
//    need_flush_ = true;
//  }
//private:
//  bool need_flush_;
//};
//
//class TESTFlushBlockPolicyFactory : public FlushBlockPolicyFactory
//{
//public:
//  TESTFlushBlockPolicyFactory() {}
//  const char* Name() const override {
//    return "TESTFlushBlockPolicyFactory";
//  }
//  FlushBlockPolicy* NewFlushBlockPolicy(const BlockBasedTableOptions& table_options,
//                                        const BlockBuilder& data_block_builder) const override {
//    return new TESTFlushBlockPolicy;
//  }
//};

struct TestArgs {
  common::CompressionType compression_;
  uint32_t format_version_;
  std::string db_path_;
  common::Options *options_;
  BlockBasedTableOptions table_options_;
  size_t block_cache_size_;

  TestArgs() : compression_(common::CompressionType::kNoCompression),
               format_version_(3),
               db_path_(util::test::TmpDir() + "/interal_iterator_test_base"),
               options_(nullptr),
               block_cache_size_(0)
  {}
  void reset ()
  {
    delete options_;
  }
  void build_default_options()
  {
    reset();
    options_ = new common::Options();
    int block_size = 16 * 1024;
    table_options_.block_size = block_size;
    //table_options_.flush_block_policy_factory.reset(new TESTFlushBlockPolicyFactory());
    table_options_.block_cache = cache::NewLRUCache(block_cache_size_ == 0 ? 50000 : block_cache_size_, 1);
    options_->table_factory.reset(NewExtentBasedTableFactory(table_options_));
    options_->disable_auto_compactions = true;
    options_->env = util::Env::Default();
    int db_total_write_buffer_size = 64 * 1024 * 1024;
    options_->db_total_write_buffer_size = db_total_write_buffer_size;
    int write_buffer_size = db_total_write_buffer_size;
    options_->write_buffer_size = write_buffer_size;

    if (options_->db_paths.size() == 0) {
      options_->db_paths.emplace_back(db_path_, std::numeric_limits<uint64_t>::max());
    }

    auto factory = std::make_shared<memtable::SkipListFactory>();
    options_->memtable_factory = factory;
    db::WriteBufferManager *wb = new db::WriteBufferManager(0);  // no limit space
    assert(wb != nullptr);
    options_->write_buffer_manager.reset(wb);
  }
};

struct TestContext
{
  common::Options *options_;
  common::DBOptions db_options_;
  util::EnvOptions env_options_;
  common::ImmutableDBOptions idb_options_;
  common::MutableCFOptions mutable_cf_options_;
  common::ImmutableCFOptions icf_options_;

  TestContext(common::Options *options) : options_(options),
                                          db_options_(*options_),
                                          env_options_(db_options_),
                                          idb_options_(*options_),
                                          mutable_cf_options_(*options_),
                                          icf_options_(*options_)
  {
  }
};

class InternalIteratorTestBase : public testing::Test
{
public:
  InternalIteratorTestBase() : context_(nullptr),
                               env_(nullptr),
                               global_ctx_(nullptr),
                               write_buffer_manager_(nullptr),
                               next_file_number_(2),
                               version_set_(nullptr),
                               descriptor_log_(nullptr),
                               internal_comparator_(util::BytewiseComparator()),
                               extent_writer_(nullptr),
                               //block_cache_(nullptr),
                               table_cache_(nullptr),
                               row_size_(0),
                               key_size_(0),
                               //mutex_(monitor::mutex_dbimpl_key),
                               level_(1)
  {}
  virtual ~InternalIteratorTestBase()
  {
    destroy();
    reset();
  }

  void init(const TestArgs &args);
  void reset();
  void destroy();
  void open_extent_writer();
  void append_block(const int64_t start_key,
                    const int64_t end_key,
                    const bool flush_block);
  void append_rows(const int64_t start_key,
                   const int64_t end_key,
                   bool flush_block);
  void close_extent_builder();
  void make_key(char *buf,
                const int64_t size,
                const int64_t key);
  void make_value(char *buf,
                  const int64_t size,
                  const int64_t key);

protected:
  static const int64_t INDEX_ID = 0;
protected:
  // env
  TestContext *context_;
  util::Env *env_;
  std::unique_ptr<util::Directory, memory::ptr_destruct_delete<util::Directory>> db_dir_;
  std::string dbname_;
  db::GlobalContext *global_ctx_;
  db::WriteBufferManager *write_buffer_manager_;
  // extent space manager
  db::FileNumber next_file_number_;
  // storage
  unique_ptr<storage::StorageManager> storage_manager_;
  // stroage logger
  db::VersionSet *version_set_;
  db::log::Writer *descriptor_log_;
  // extent budiler
  storage::ChangeInfo change_info_;
  db::InternalKeyComparator internal_comparator_;
  std::string compression_dict_;
  storage::ColumnFamilyDesc cf_desc_;
  db::MiniTables mini_tables_;
  std::unique_ptr<table::ExtentWriter> extent_writer_;
  // scan
  //std::shared_ptr<cache::Cache> block_cache_;
  std::unique_ptr<db::TableCache> table_cache_;
  std::shared_ptr<cache::Cache> clock_cache_;
  int64_t row_size_;
  int64_t key_size_;
  // used to flush extent
  //monitor::InstrumentedMutex mutex_;
  int64_t level_;
};

void InternalIteratorTestBase::destroy()
{
  ASSERT_OK(db::DestroyDB(dbname_, *context_->options_));
  delete context_->options_;
  context_->options_ = nullptr;
}

void InternalIteratorTestBase::reset()
{
  env_ = nullptr;
  db_dir_.reset();
  mini_tables_.metas.clear();
  mini_tables_.props.clear();
  extent_writer_.reset();
  StorageLogger::get_instance().destroy();
  descriptor_log_ = nullptr;
  storage_manager_.reset();
  ExtentSpaceManager::get_instance().destroy();
  ExtentMetaManager::get_instance().destroy();
  WriteExtentJobScheduler::get_instance().stop();
  smartengine::common::Options* options = nullptr;
  if (nullptr != context_) {
    options = context_->options_;
    delete context_;
    context_ = nullptr;
  }
  if (nullptr != version_set_) {
    delete version_set_;
    version_set_ = nullptr;
  }
  //block_cache_.reset();
  clock_cache_.reset();
  table_cache_.reset();
  //block_cache_size_ = 0;
  row_size_ = 0;
  key_size_ = 0;
  if (nullptr != global_ctx_) {
    delete global_ctx_;
  }
  if (nullptr != write_buffer_manager_) {
    delete write_buffer_manager_;
  }
  delete options;
}

void InternalIteratorTestBase::init(const TestArgs &args)
{
  reset();
  context_ = new TestContext(args.options_);
  env_ = context_->options_->env;
  dbname_ = context_->options_->db_paths[0].path;
  env_->DeleteDir(dbname_);
  env_->CreateDir(dbname_);
  util::Directory *ptr = nullptr;
  env_->NewDirectory(dbname_, ptr);
  db_dir_.reset(ptr);

  // new
  clock_cache_ = cache::NewLRUCache(50000, 1);
  global_ctx_ = new db::GlobalContext(dbname_, *context_->options_);
  write_buffer_manager_ = new db::WriteBufferManager(0);
  version_set_ = new db::VersionSet(dbname_,
                                    &context_->idb_options_,
                                    context_->env_options_,
                                    reinterpret_cast<cache::Cache*>(table_cache_.get()),
                                    write_buffer_manager_);
  table_cache_.reset(new db::TableCache(context_->icf_options_, clock_cache_.get()));

  // init storage logger
  global_ctx_->env_ = env_;
  global_ctx_->cache_ = clock_cache_.get();
  global_ctx_->write_buf_mgr_ = write_buffer_manager_;
  version_set_->init(global_ctx_);

  StorageLogger::get_instance().TEST_reset();
  StorageLogger::get_instance().init(env_,
                                     dbname_,
                                     context_->env_options_,
                                     context_->idb_options_,
                                     version_set_,
                                     1 * 1024 * 1024 * 1024);
  StorageLogger::get_instance().set_log_writer(1);

  // storage manager
  db::CreateSubTableArgs subtable_args;
  //subtable_args.index_id_ = 1;
  storage_manager_.reset(new storage::StorageManager(nullptr,
                                                     context_->env_options_,
                                                     context_->icf_options_,
                                                     context_->mutable_cf_options_));
  storage_manager_->init();

  ExtentMetaManager::get_instance().init();
  // space manager
  ExtentSpaceManager::get_instance().init(env_, global_ctx_->env_options_, context_->db_options_);
  ExtentSpaceManager::get_instance().create_table_space(0);
  WriteExtentJobScheduler::get_instance().start(env_, 1);
  row_size_ = 16;
  key_size_ = 32;
  level_ = 1;
}

void InternalIteratorTestBase::open_extent_writer()
{
  int ret = common::Status::kOk;
  mini_tables_.change_info_ = &change_info_;
  ASSERT_EQ(StorageLogger::get_instance().begin(storage::MINOR_COMPACTION), common::Status::kOk);
  //mini_tables_.change_info_->task_type_ = db::TaskType::SPLIT_TASK;
  mini_tables_.table_space_id_ = 0;
  cf_desc_.column_family_id_ = 1;
  storage::LayerPosition output_layer_position(level_, 0);
  common::CompressionType compression_type = get_compression_type(context_->icf_options_, level_);
  /**TODO(Zhao Dongsheng): The way of obtaining the block cache is not elegent. */
  ExtentBasedTableFactory *tmp_factory = reinterpret_cast<ExtentBasedTableFactory *>(
      context_->icf_options_.table_factory);
  schema::TableSchema table_schema;
  table_schema.set_index_id(cf_desc_.column_family_id_);
  ExtentWriterArgs args(std::string(),
                        mini_tables_.table_space_id_,
                        tmp_factory->table_options().block_restart_interval,
                        context_->icf_options_.env->IsObjectStoreInited() ? storage::OBJECT_EXTENT_SPACE : storage::FILE_EXTENT_SPACE,
                        table_schema,
                        &internal_comparator_,
                        output_layer_position,
                        tmp_factory->table_options().block_cache.get(),
                        context_->icf_options_.row_cache.get(),
                        compression_type,
                        &change_info_);
  ExtentWriter *extent_writer_ptr = new ExtentWriter();
  se_assert(extent_writer_ptr);
  ret = extent_writer_ptr->init(args);
  ASSERT_EQ(common::Status::kOk, ret);
  extent_writer_.reset(extent_writer_ptr);
}

// NOTICE! the end_key will in the next block/extent
void InternalIteratorTestBase::append_block(const int64_t start_key,
                                            const int64_t end_key,
                                            const bool flush_block)
{
  append_rows(start_key, end_key, flush_block);
}

void InternalIteratorTestBase::make_key(char *buf,
                                        const int64_t size,
                                        const int64_t key)
{
  memset(buf, 0, size);
  snprintf(buf, size, "%04ld%010ld", INDEX_ID, key);
}

void InternalIteratorTestBase::make_value(char *buf,
                                          const int64_t size,
                                          const int64_t key)
{
  memset(buf, 0, size);
  snprintf(buf, size, "%010ld", key);
}

void InternalIteratorTestBase::append_rows(const int64_t start_key,
                                           const int64_t end_key,
                                           bool flush_block)
{
  int ret = common::Status::kOk;
  ASSERT_TRUE(nullptr != extent_writer_.get());
  int64_t commit_log_seq = 0;
  char key_buf[key_size_];
  char row_buf[row_size_];
  memset(key_buf, 0, key_size_);
  memset(row_buf, 0, row_size_);

  for (int64_t key = start_key; key <= end_key; key++) {
    make_key(key_buf, key_size_, key);
    if (key == end_key) {
      if (flush_block) {
        extent_writer_->test_force_flush_data_block();
        //reinterpret_cast<TESTFlushBlockPolicy *>(
        //    reinterpret_cast<ExtentBasedTableBuilder *>(
        //    extent_builder_.get())->rep_->flush_block_policy.get())->set_need_flush();
      }
    }
    db::InternalKey ikey(common::Slice(key_buf, strlen(key_buf)), 10 /*sequence*/, db::kTypeValue);
    make_value(row_buf, row_size_, key);
    ret = extent_writer_->append_row(ikey.Encode(), common::Slice(row_buf, row_size_));
    se_assert(common::Status::kOk == ret);
  }
}

void InternalIteratorTestBase::close_extent_builder()
{
  int ret = common::Status::kOk;
  int64_t commit_seq = 0;
  ret = extent_writer_->finish(nullptr /*extent_infos*/);
  se_assert(common::Status::kOk == ret);
  ASSERT_EQ(StorageLogger::get_instance().commit(commit_seq), common::Status::kOk);
  ASSERT_EQ(storage_manager_->apply(*(mini_tables_.change_info_), false), common::Status::kOk);
  mini_tables_.metas.clear();
  mini_tables_.props.clear();
  mini_tables_.change_info_->clear();
}
} // namespace table
} // namespace smartengine
