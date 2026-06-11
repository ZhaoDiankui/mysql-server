
#include "table/extent_writer.h"
#include "db/version_set.h"
#include "options/db_options.h"
#include "options/options.h"
#include "storage/change_info.h"
#include "storage/extent_meta_manager.h"
#include "storage/storage_logger.h"
#include "util/sync_point.h"
#include "util/testharness.h"
#include "util/testutil.h"

static const std::string test_dir = smartengine::util::test::TmpDir() + "/extent_writer_test";

namespace smartengine
{
using namespace common;
using namespace storage;
using namespace util;

namespace table
{

class ExtentWriterTest : public testing::Test
{
public:
  ExtentWriterTest();
  virtual ~ExtentWriterTest();

  virtual void SetUp();
  virtual void TearDown();

public:
  ExtentWriter *writer_;
  schema::TableSchema table_schema_;
  db::InternalKeyComparator comparator_;
  LayerPosition position_;
  ChangeInfo change_info_;
};

ExtentWriterTest::ExtentWriterTest()
    : writer_(nullptr),
      table_schema_(),
      comparator_(util::BytewiseComparator()),
      position_(),
      change_info_()
{}

ExtentWriterTest::~ExtentWriterTest()
{}

void ExtentWriterTest::SetUp()
{
  int ret = Status::kOk;
  int64_t table_space_id = 1;
  std::string dbname(test_dir);
  util::EnvOptions env_options;
  DBOptions db_options;
  db_options.db_paths.push_back(DbPath(dbname, 0));
  ImmutableDBOptions immutable_db_options;
  db::VersionSet *version_set = reinterpret_cast<db::VersionSet *>(0x01);

  ret = WriteExtentJobScheduler::get_instance().start(util::Env::Default(), 1);
  ASSERT_EQ(Status::kOk, ret);

  ret = StorageLogger::get_instance().init(util::Env::Default(),
                                           dbname,
                                           env_options,
                                           immutable_db_options,
                                           version_set,
                                           64 * 1024 * 1024);
  ASSERT_EQ(Status::kOk, ret);
  ret = StorageLogger::get_instance().set_log_writer(1);
  ASSERT_EQ(Status::kOk, ret);
  ret = StorageLogger::get_instance().begin(storage::FLUSH);
  ASSERT_EQ(Status::kOk, ret);

  ret = ExtentMetaManager::get_instance().init();
  ASSERT_EQ(Status::kOk, ret);

  ret = ExtentSpaceManager::get_instance().init(util::Env::Default(), env_options, db_options);
  ASSERT_EQ(Status::kOk, ret);
  ret = ExtentSpaceManager::get_instance().create_table_space(table_space_id);

  std::string cluster_id("ppl");
  table_schema_.set_index_id(1);

  ExtentWriterArgs args;
  args.cluster_id_ = cluster_id;
  args.table_space_id_ = table_space_id;
  args.block_restart_interval_ = 16;
  args.extent_space_type_ = storage::FILE_EXTENT_SPACE;
  args.table_schema_ = table_schema_;
  args.internal_key_comparator_ = &comparator_;
  args.output_position_ = position_;
  args.block_cache_ = nullptr;
  args.row_cache_ = nullptr;
  args.compress_type_ = common::kNoCompression;
  args.change_info_ = &change_info_; 

  writer_ = new ExtentWriter();
  ASSERT_TRUE(nullptr != writer_);
  ASSERT_EQ(Status::kOk, writer_->init(args));
}

void ExtentWriterTest::TearDown()
{
  int ret = Status::kOk;
  int64_t lsn = 0;
  delete writer_;
  writer_ = nullptr;

  ret = StorageLogger::get_instance().commit(lsn);
  ASSERT_EQ(Status::kOk, ret);

  StorageLogger::get_instance().destroy();
  ExtentMetaManager::get_instance().destroy();
  ExtentSpaceManager::get_instance().destroy();
  WriteExtentJobScheduler::get_instance().stop();
}

TEST_F(ExtentWriterTest, inject_write_error)
{
  int ret = Status::kOk;
  std::string user_key("zds");
  std::string value("ppl");
  db::InternalKey ikey(user_key, 100, db::kTypeValue);

  SyncPoint::GetInstance()->SetCallBack("IOExtent::write_failed",
      [&](void *arg){
        int *ret_ptr = reinterpret_cast<int *>(arg);
        *ret_ptr = Status::kIOError;
      });
  SyncPoint::GetInstance()->EnableProcessing();

  ret = writer_->append_row(ikey.Encode(), value);
  ASSERT_EQ(Status::kOk, ret);

  ret = writer_->finish(nullptr);
  ASSERT_EQ(Status::kIOError, ret);

  SyncPoint::GetInstance()->DisableProcessing();
}

} // namespace table
} // namespace smartengine

int main(int argc, char **argv)
{
  smartengine::util::test::remove_dir(test_dir.c_str());
  smartengine::util::Env::Default()->CreateDir(test_dir);
  std::string log_path = smartengine::util::test::TmpDir() + "/extent_writer_test.log";
  smartengine::logger::Logger::get_log().init(log_path.c_str(), smartengine::logger::DEBUG_LEVEL);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}