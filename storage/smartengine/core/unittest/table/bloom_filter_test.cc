#include "table/bloom_filter.h"
#include "util/status.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace smartengine
{
using namespace common;
using namespace util;

namespace table
{
TEST(BloomFilterWriter, writer_init)
{
  int ret = Status::kOk;
  BloomFilterWriter bf_writer;

  // Init test
  ret = bf_writer.init(0, 1);
  ASSERT_EQ(Status::kInvalidArgument, ret);

  ret = bf_writer.init(1, 0);
  ASSERT_EQ(Status::kInvalidArgument, ret);

  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);

  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kInitTwice, ret);
}

TEST(BloomFilterWriter, writer_add)
{
  int ret = Status::kOk;
  BloomFilterWriter bf_writer;

  // Not init
  ret = bf_writer.add(Slice());
  ASSERT_EQ(Status::kNotInit, ret);

  // Invalid argument
  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);
  ret = bf_writer.add(Slice());
  ASSERT_EQ(Status::kInvalidArgument, ret);

  // Succcess to add
  const char *value = "ppl";
  ret = bf_writer.add(Slice(value, strlen(value)));
  ASSERT_EQ(Status::kOk, ret);
}

TEST(BloomFilterWriter, writer_build)
{
  int ret = Status::kOk;
  Slice bloom_filter;
  BloomFilterWriter bf_writer;

  // Not init
  ret = bf_writer.build(bloom_filter);
  ASSERT_EQ(Status::kNotInit, ret);

  // empty bloom filter
  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);
  ret = bf_writer.build(bloom_filter);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_TRUE(bloom_filter.empty());

  // One value
  const char *str1 = "zds";
  ret = bf_writer.add(Slice(str1, strlen(str1)));
  ASSERT_EQ(Status::kOk, ret);
  ret = bf_writer.build(bloom_filter);
  ASSERT_EQ(64, bloom_filter.size());
}

TEST(BloomFilterWriter, writer_calculate_space_size)
{
  int32_t size = 0;
  int32_t cache_line_num = 0;

  BloomFilterWriter::calculate_space_size(0, 0, size, cache_line_num);
  ASSERT_EQ(64, size);
  ASSERT_EQ(1, cache_line_num);
  
  BloomFilterWriter::calculate_space_size(1, 10, size, cache_line_num);
  ASSERT_EQ(64, size);
  ASSERT_EQ(1, cache_line_num);

  BloomFilterWriter::calculate_space_size(52, 10, size, cache_line_num);
  ASSERT_EQ(64 * 3, size);
  ASSERT_EQ(3, cache_line_num);
}

TEST(BloomFilterReader, reader_init)
{
  int ret = Status::kOk;
  BloomFilterReader bf_reader;
  const char *fake_bloom_filter = "summer";

  // Invalid argument
  // empty bloom filter
  ret = bf_reader.init(Slice(), BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  // invalid bloom filter size, less than CACHE_LINE_SIZE
  ret = bf_reader.init(Slice(fake_bloom_filter, strlen(fake_bloom_filter)), BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  // invalid bloom filter size, not aligned to CACHE_LINE_SIZE
  ret = bf_reader.init(Slice(fake_bloom_filter, strlen(fake_bloom_filter)), 0);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  // invalid probe num
  ret = bf_reader.init(Slice(fake_bloom_filter, CACHE_LINE_SIZE), 0);
  ASSERT_EQ(Status::kInvalidArgument, ret);

  // Succcess init
  ret = bf_reader.init(Slice(fake_bloom_filter, CACHE_LINE_SIZE), BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);
  ret = bf_reader.init(Slice(fake_bloom_filter, CACHE_LINE_SIZE), BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kInitTwice, ret);
}

TEST(BloomFilterReader, reader_check)
{
  int ret = Status::kOk;
  Slice bloom_filter;
  BloomFilterWriter bf_writer;
  BloomFilterReader bf_reader;
  bool may_exist = false;
  const char *exist_value = "zds";
  const char *not_exist_value = "ppl";

  // Construct valid  bloom filter.
  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);
  ret = bf_writer.add(Slice(exist_value, strlen(exist_value)));
  ASSERT_EQ(Status::kOk, ret);
  ret = bf_writer.build(bloom_filter);
  ASSERT_EQ(Status::kOk, ret);
  
  // Not init
  ret = bf_reader.check(Slice(), may_exist);
  ASSERT_EQ(Status::kNotInit, ret);

  // Init
  ret = bf_reader.init(bloom_filter, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);

  // Invalid argument;
  ret = bf_reader.check(Slice(), may_exist);
  ASSERT_EQ(Status::kInvalidArgument, ret);

  // Exist value
  ret = bf_reader.check(Slice(exist_value, strlen(exist_value)), may_exist);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_TRUE(may_exist);

  // Not exist value
  ret = bf_reader.check(Slice(not_exist_value, strlen(not_exist_value)), may_exist);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_FALSE(may_exist);
}

TEST(BloomFilter, basic_check)
{
  int ret = Status::kOk;
  Slice bloom_filter;
  bool may_exist = false;
  BloomFilterWriter bf_writer;
  BloomFilterReader bf_reader;

  // Build bloom filter.
  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);

  for (uint32_t i = 0; i < 100; i += 2) {
    ret = bf_writer.add(Slice(reinterpret_cast<char *>(&i), sizeof(i)));
    ASSERT_EQ(Status::kOk, ret);
  }

  ret = bf_writer.build(bloom_filter);
  ASSERT_EQ(Status::kOk, ret);

  // Init bloom filter reader.
  ret = bf_reader.init(bloom_filter, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);

  // Check exist value.
  for (uint32_t i = 0; i < 100; i += 2) {
    ret = bf_reader.check(Slice(reinterpret_cast<char *>(&i), sizeof(i)), may_exist);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_TRUE(may_exist);
  }

  // Check not exist value.
  for (uint32_t i = 1; i < 100; i += 2) {
    ret = bf_reader.check(Slice(reinterpret_cast<char *>(&i), sizeof(i)), may_exist);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_FALSE(may_exist);
  }
}

TEST(BloomFilter, random_check)
{
  int ret = Status::kOk;
  Slice bloom_filter;
  bool may_exist = false;
  int64_t value = 0;
  std::vector<int64_t> exist_values;
  BloomFilterWriter bf_writer;
  BloomFilterReader bf_reader;

  // Build bloom filter.
  ret = bf_writer.init(BloomFilter::DEFAULT_PER_KEY_BITS, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);


  test::RandomInt64Generator generator(1, 1000000);
  for (uint32_t i = 0; i < 5000; ++i) {
    value = generator.generate();
    exist_values.push_back(value);
    ret = bf_writer.add(Slice(reinterpret_cast<char *>(&value), sizeof(value)));
    ASSERT_EQ(Status::kOk, ret);
  }

  ret = bf_writer.build(bloom_filter);
  ASSERT_EQ(Status::kOk, ret);

  // Init bloom filter reader.
  ret = bf_reader.init(bloom_filter, BloomFilter::DEFAULT_PROBE_NUM);
  ASSERT_EQ(Status::kOk, ret);

  // Check exist value
  for (uint32_t i = 0; i < exist_values.size(); ++i) {
    value = exist_values[i];
    ret = bf_reader.check(Slice(reinterpret_cast<char *>(&value), sizeof(value)), may_exist);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_TRUE(may_exist);
  }
}

} // namespace table
} // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}