#include "table/large_object.h"
#include "db/db_iter.h"
#include "db/db_test_util.h"

static const std::string test_dir = smartengine::util::test::TmpDir() + "/large_object_test";

namespace smartengine
{
using namespace common;
using namespace db;

namespace table
{
class LargeObjectTest : public db::DBTestBase
{
public:
  LargeObjectTest() : DBTestBase("large_object_test") {}
  ~LargeObjectTest() {}

protected:
  std::string generate_random_string(int64_t length)
  {
    // use current time as seed
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<int> distribution(32, 126); // ASCII visible range

    // generate
    std::string result;
    result.reserve(length);
    for (int64_t i = 0; i < length; ++i) {
        result.push_back(static_cast<char>(distribution(generator)));
    }

    return result;  
  }
};

TEST_F(LargeObjectTest, large_object)
{
  int ret = Status::kOk;
  common::Options options;
  CreateAndReopenWithCF({"summer"}, options); 

  // Prepare data
  std::vector<std::string> random_strs;
  random_strs.push_back(generate_random_string(storage::MAX_EXTENT_SIZE));
  random_strs.push_back(generate_random_string(storage::MAX_EXTENT_SIZE * 2));
  random_strs.push_back(generate_random_string(storage::MAX_EXTENT_SIZE / 2 + storage::MAX_EXTENT_SIZE));

  ASSERT_OK(Put(1, "0", random_strs[0]));
  ASSERT_OK(Put(1, "1", random_strs[1]));
  ASSERT_OK(Put(1, "2", random_strs[2]));

  ASSERT_OK(Flush(1));

  // Check data through get
  ReopenWithColumnFamilies({"default", "summer"}, options);
  ASSERT_EQ(random_strs[0], Get(1, "0"));
  ASSERT_EQ(random_strs[1], Get(1, "1"));
  ASSERT_EQ(random_strs[2], Get(1, "2"));
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_allocated_size(memory::ModId::kLargeObject));

  // Check data through scan
  ReopenWithColumnFamilies({"default", "summer"}, options);
  int64_t row_count = 0;
  util::Arena arena;
  InternalIterator *internal_iterator = NewInternalIterator(&arena, get_column_family_handle(1));
  ASSERT_TRUE(nullptr != internal_iterator);
  db::ArenaWrappedDBIter *scan_iterator = db::NewArenaWrappedDbIterator(options.env,
                                                                   ReadOptions(),
                                                                   ImmutableCFOptions(options),
                                                                   util::BytewiseComparator(),
                                                                   kMaxSequenceNumber);
  ASSERT_TRUE(nullptr != scan_iterator);
  scan_iterator->SetIterUnderDBIter(internal_iterator);
  scan_iterator->SeekToFirst();
  while (scan_iterator->Valid()) {
    ASSERT_EQ(random_strs[row_count], scan_iterator->value().ToString());
    ++row_count;
    scan_iterator->Next();
  }
  ASSERT_EQ(3, row_count);

  // Release the super version.
  MOD_DELETE_OBJECT(ArenaWrappedDBIter, scan_iterator);
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_allocated_size(memory::ModId::kLargeObject));

  // Compress large object with ZSTD
  options.compression_per_level.push_back(kZSTD);
  ReopenWithColumnFamilies({"default", "summer"}, options);
  ASSERT_OK(Put(1, "3", random_strs[0]));
  ASSERT_OK(Put(1, "4", random_strs[1]));
  ASSERT_OK(Put(1, "5", random_strs[2]));
  ASSERT_OK(Flush(1));
  ASSERT_EQ(random_strs[0], Get(1, "0"));
  ASSERT_EQ(random_strs[1], Get(1, "1"));
  ASSERT_EQ(random_strs[2], Get(1, "2"));
  ASSERT_EQ(random_strs[0], Get(1, "3"));
  ASSERT_EQ(random_strs[1], Get(1, "4"));
  ASSERT_EQ(random_strs[2], Get(1, "5"));
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_allocated_size(memory::ModId::kLargeObject));

  // Check data through get
  ReopenWithColumnFamilies({"default", "summer"}, options);
  ASSERT_EQ(random_strs[0], Get(1, "0"));
  ASSERT_EQ(random_strs[1], Get(1, "1"));
  ASSERT_EQ(random_strs[2], Get(1, "2"));
  ASSERT_EQ(random_strs[0], Get(1, "3"));
  ASSERT_EQ(random_strs[1], Get(1, "4"));
  ASSERT_EQ(random_strs[2], Get(1, "5")); 
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_allocated_size(memory::ModId::kLargeObject));

  // Check data through scan
  internal_iterator = NewInternalIterator(&arena, get_column_family_handle(1));
  ASSERT_TRUE(nullptr != internal_iterator);
  scan_iterator = db::NewArenaWrappedDbIterator(options.env,
                                                ReadOptions(),
                                                ImmutableCFOptions(options),
                                                util::BytewiseComparator(),
                                                kMaxSequenceNumber);
  ASSERT_TRUE(nullptr != scan_iterator);
  scan_iterator->SetIterUnderDBIter(internal_iterator);
  row_count = 0;
  scan_iterator->SeekToFirst();
  while (scan_iterator->Valid()) {
    ASSERT_EQ(random_strs[row_count % 3], scan_iterator->value().ToString());
    ++row_count;
    scan_iterator->Next();
  }
  ASSERT_EQ(6, row_count);

  // Release the super version.
  MOD_DELETE_OBJECT(ArenaWrappedDBIter, scan_iterator);
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_allocated_size(memory::ModId::kLargeObject));
}

} // namespace table
} // namespace smartengine

int main(int argc, char **argv)
{
  smartengine::util::test::remove_dir(test_dir.c_str());
  smartengine::util::Env::Default()->CreateDir(test_dir);
  std::string log_path = smartengine::util::test::TmpDir() + "/large_object_test.log";
  smartengine::logger::Logger::get_log().init(log_path.c_str(), smartengine::logger::DEBUG_LEVEL);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}