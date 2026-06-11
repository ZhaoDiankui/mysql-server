#include <string>
#include <cstdlib>
#include <ctime>
#include "options/options.h"
#include "util/file_name.h"
#include "util/testharness.h"
#include "util/testutil.h"
#include "util/compress/compressor.h"
#include "util/compress/compressor_factory.h"
#include "util/compress/compressor_helper.h"

static const std::string test_dir = smartengine::util::test::TmpDir() + "/compressor_test";

namespace smartengine {
using namespace common;

namespace util {
class CompressorTest : public testing::Test
{
public:
  int check_short_text(Compressor *compressor);

  int check_long_text(Compressor *compressor);

private:
  int compress_and_uncompress(Compressor *compressor,
                              const char *raw_data,
                              const int64_t raw_data_size);

  int generate_random_str(const int64_t size, char *&str);

  void destroy_str(char *str);

protected:
  static const int64_t COMPRESSOR_TYPE_COUNT = 3;
  static CompressionType compressor_arr[COMPRESSOR_TYPE_COUNT];
};

CompressionType CompressorTest::compressor_arr[CompressorTest::COMPRESSOR_TYPE_COUNT]
    = {kLZ4Compression, kZlibCompression, kZSTD};

int CompressorTest::check_short_text(Compressor *compressor)
{
  int ret = Status::kOk;
  const char *short_text = "wesql-smartengine-wesql-smartengine";
  //const char *short_text = "aaaaaaaa";
  const int64_t short_text_size = strlen(short_text);

  if (FAILED(compress_and_uncompress(compressor,
                                     short_text,
                                     short_text_size))) {
    SE_LOG(WARN, "failed to compress or uncompress short text", K(ret));
  }

  return ret;
}

int CompressorTest::check_long_text(Compressor *compressor)
{
  int ret = Status::kOk;
  const int64_t long_text_size = 64 * 1024; //16KB
  char *long_text = nullptr;

  if (FAILED(generate_random_str(long_text_size, long_text))) {
    SE_LOG(WARN, "failed to generate long text", K(ret), K(long_text_size));
  } else if (IS_NULL(long_text)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "the long test must not be nullptr", K(ret), KP(long_text));
  } else if (FAILED(compress_and_uncompress(compressor, long_text, long_text_size))) {
    SE_LOG(WARN, "failed to compress and uncompress data", K(ret));
  } else {
    //succeed
  }

  if (long_text) {
    destroy_str(long_text);
  }

  return ret;
}

int CompressorTest::compress_and_uncompress(Compressor *compressor,
                                            const char *raw_data,
                                            const int64_t raw_data_size)
{
  int ret = Status::kOk;
  int64_t max_compress_size = 0;

  /**For compress procedure*/
  char *compress_buf = nullptr;
  int64_t compressed_data_size = 0;

  /**For uncompress procedure*/
  char *uncompress_buf = nullptr;
  int64_t uncompressed_data_size = 0;

  if (IS_NULL(compressor) || IS_NULL(raw_data) || (raw_data_size <= 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(compressor), KP(raw_data), K(raw_data_size));
  } else if (FAILED(compressor->get_max_compress_size(raw_data_size, max_compress_size))) {
    SE_LOG(WARN, "Failed to get max compress size", K(ret), KE(compressor->get_compress_type()));
  } else if (IS_NULL(compress_buf = new char[max_compress_size])) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "Failed to allocate memory for compress buf", K(ret), K(max_compress_size));
  } else if (FAILED(compressor->compress(raw_data,
                                         raw_data_size,
                                         compress_buf,
                                         max_compress_size,
                                         compressed_data_size))) {
    SE_LOG(WARN, "failed to compress data", K(ret));
  } else if (IS_NULL(uncompress_buf = new char[max_compress_size])) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "Failed to allocate memory for uncompress buf", K(ret), K(max_compress_size));
  } else if (FAILED(compressor->uncompress(compress_buf,
                                           compressed_data_size,
                                           uncompress_buf,
                                           max_compress_size,
                                           uncompressed_data_size))) {
    SE_LOG(WARN, "failed to uncompress data", K(ret));
  } else if (raw_data_size != uncompressed_data_size) {
    ret = Status::kCorruption;
    SE_LOG(WARN, "the data is corrupted, because the size is mismatch", K(ret),
        K(raw_data_size), K(uncompressed_data_size));
  } else if (0 != memcmp(raw_data, uncompress_buf, raw_data_size)) {
    ret = Status::kCorruption;
    SE_LOG(WARN, "the data is corrupted, because the content is mismatch", K(ret));
  } else {
    //succeed
  }

  if (nullptr != compress_buf) {
    delete [] compress_buf;
    compress_buf = nullptr;
  }

  if (nullptr != uncompress_buf) {
    delete [] uncompress_buf;
    uncompress_buf = nullptr;
  }

  return ret;
}

int CompressorTest::generate_random_str(const int64_t size, char *&str)
{
  int ret = Status::kOk;

  if (IS_NULL(str = new char[size])) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "failed to allocate memory for str", K(ret), K(size));
  } else {
    std::srand(std::time(nullptr));

    for (int i = 0; i < size; ++i) {
      str[i] = '0' + rand() % 78;
    }
  }

  return ret;
}

void CompressorTest::destroy_str(char *str)
{
  if (str) {
    delete [] str;
    str = nullptr;
  }
}

TEST_F(CompressorTest, compress_invalid_argument)
{
  SE_LOG(INFO, "=============Case:compress_invalid_argument==================");
  int ret = Status::kOk;
  Compressor *compressor = nullptr;
  const char *raw_data = nullptr;
  int64_t raw_data_size = 0;
  char *compress_buf = nullptr;
  int64_t compress_buf_size = 0;
  int64_t compressed_data_size = 0;
  char tmp_buf[3] = {0};

  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    CompressorFactory::get_instance().alloc_compressor(compressor_arr[i], compressor);
    ASSERT_TRUE(nullptr != compressor);

    //invalid raw_data
    raw_data = nullptr;
    raw_data_size = 10;
    compress_buf = tmp_buf;
    compress_buf_size = 3;
    ret = compressor->compress(raw_data, raw_data_size, compress_buf, compress_buf_size, compressed_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //invalid raw_data_size
    raw_data = "smartengine";
    raw_data_size = 0;
    compress_buf = tmp_buf;
    compress_buf_size = 3;
    ret = compressor->compress(raw_data, raw_data_size, compress_buf, compress_buf_size, compressed_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //invalid compress_buf
    raw_data = "smartengine";
    raw_data_size = strlen(raw_data);
    compress_buf = nullptr;
    compress_buf_size = 3;
    ret = compressor->compress(raw_data, raw_data_size, compress_buf, compress_buf_size, compressed_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //invalid compress_buf_size
    raw_data = "smartengine";
    raw_data_size = strlen(raw_data);
    compress_buf = tmp_buf;
    compress_buf_size = 0;
    ret= compressor->compress(raw_data, raw_data_size, compress_buf, compress_buf_size, compressed_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //compress_buf_size is not enough
    raw_data = "smartengine";
    raw_data_size = strlen(raw_data);
    compress_buf = tmp_buf;
    compress_buf_size = 3;
    ret = compressor->compress(raw_data, raw_data_size, compress_buf, compress_buf_size, compressed_data_size);
    ASSERT_EQ(Status::kOverLimit, ret);

    CompressorFactory::get_instance().free_compressor(compressor);
  }
  SE_LOG(INFO, "=============Case:compress_invalid_argument==================");
}

TEST_F(CompressorTest, uncompress_invalid_argument)
{
  SE_LOG(INFO, "=============Case:uncompress_invalid_argument==================");
  int ret = Status::kOk;
  Compressor *compressor = nullptr;
  const char *compressed_data = nullptr;
  int64_t compressed_data_size = 0;
  char *raw_buf = nullptr;
  int64_t raw_buf_size = 0;
  int64_t raw_data_size = 0;
  char tmp_buf[3] = {0};

  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    CompressorFactory::get_instance().alloc_compressor(compressor_arr[i], compressor);
    ASSERT_TRUE(nullptr != compressor);

    //invalid compressed_data
    compressed_data = nullptr;
    compressed_data_size = 3;
    raw_buf = tmp_buf;
    raw_buf_size = 3;
    ret = compressor->uncompress(compressed_data, compressed_data_size, raw_buf, raw_buf_size, raw_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //invalid compressed_data_size
    compressed_data = "smartengine";
    compressed_data_size = 0;
    raw_buf = tmp_buf;
    raw_buf_size = 3;
    ret = compressor->uncompress(compressed_data, compressed_data_size, raw_buf, raw_buf_size, raw_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //invalid raw_buf
    compressed_data = "smartengine";
    compressed_data_size = strlen(compressed_data);
    raw_buf = nullptr;
    raw_buf_size = 3;
    ret = compressor->uncompress(compressed_data, compressed_data_size, raw_buf, raw_buf_size, raw_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    //invalid raw_buf_size
    compressed_data = "smartengine";
    compressed_data_size = strlen(compressed_data);
    raw_buf = tmp_buf;
    raw_buf_size = 0;
    ret = compressor->uncompress(compressed_data, compressed_data_size, raw_buf, raw_buf_size, raw_data_size);
    ASSERT_EQ(Status::kInvalidArgument, ret);

    CompressorFactory::get_instance().free_compressor(compressor);
  }
  SE_LOG(INFO, "=============Case:uncompress_invalid_argument==================");
}

TEST_F(CompressorTest, short_text)
{
  SE_LOG(INFO, "=============Case:short_text==================");
  int ret = Status::kOk;
  Compressor *compressor = nullptr;

  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    CompressorFactory::get_instance().alloc_compressor(compressor_arr[i], compressor);
    ASSERT_TRUE(nullptr != compressor);

    if (SUCCED(check_short_text(compressor))) {
      SE_LOG(INFO, "Success to check short text for compression type", KE(compressor->get_compress_type()));
    } else {
      SE_LOG(ERROR, "Fail to check short text for compression type", KE(compressor->get_compress_type()));
    }
    ASSERT_EQ(Status::kOk, ret);
    CompressorFactory::get_instance().free_compressor(compressor);
  }
  SE_LOG(INFO, "=============Case:short_text==================");
}

TEST_F(CompressorTest, LongText)
{
  SE_LOG(INFO, "=============Case:long_text==================");
  int ret = Status::kOk;
  Compressor *compressor = nullptr;

  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    CompressorFactory::get_instance().alloc_compressor(compressor_arr[i], compressor);
    ASSERT_TRUE(nullptr != compressor);

    ret = check_long_text(compressor);
    ASSERT_EQ(Status::kOk, ret);

    CompressorFactory::get_instance().free_compressor(compressor);
  }
  SE_LOG(INFO, "=============Case:long_text==================");
}

TEST_F(CompressorTest, compressor_helper_compress)
{
  int ret = Status::kOk;
  const char *data = "ppl_summer_le_ppl_summer_le_ppl_summer_le";
  Slice raw_data(data, strlen(data));
  Slice compressed_data;
  CompressionType actual_compress_type;
  CompressorHelper compressor_helper;

  // invalid argument
  ret = compressor_helper.compress(Slice(), kZSTD, compressed_data, actual_compress_type); 
  ASSERT_EQ(Status::kInvalidArgument, ret);
  ret = compressor_helper.compress(raw_data, (CompressionType)100, compressed_data, actual_compress_type);
  ASSERT_EQ(Status::kInvalidArgument, ret);

  // success compress
  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    ret = compressor_helper.compress(raw_data, compressor_arr[i], compressed_data, actual_compress_type);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_EQ(compressor_arr[i], actual_compress_type);
    ASSERT_TRUE(compressed_data.size() < raw_data.size());
  }

  // not compress
  const char *not_compress_data = "abcd";
  raw_data.assign(not_compress_data, strlen(not_compress_data));
  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    ret = compressor_helper.compress(raw_data, compressor_arr[i], compressed_data, actual_compress_type);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_EQ(kNoCompression, actual_compress_type);
    ASSERT_TRUE(raw_data == compressed_data);
  }

  // check compressor memory leak
  compressor_helper.reset();
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_hold_size(memory::ModId::kCompressor));

}

TEST_F(CompressorTest, compressor_helper_uncompress)
{
  int ret = Status::kOk;
  const char *data = "aaaaaaaaaaaaaaaaaaa";
  Slice raw_data(data, strlen(data));
  char *raw_buf = new char [raw_data.size()]; 
  Slice compressed_data;
  Slice uncompressed_data;
  CompressionType actual_compress_type;
  CompressorHelper compressor_helper;
  
  // invalid argument
  ret = compressor_helper.uncompress(Slice(), kZlibCompression, raw_buf, raw_data.size(), uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  Slice fake_compressed_data(data, strlen(data));
  ret = compressor_helper.uncompress(fake_compressed_data, common::kNoCompression, raw_buf, raw_data.size(), uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  ret = compressor_helper.uncompress(fake_compressed_data, (CompressionType)100, raw_buf, raw_data.size(), uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  ret = compressor_helper.uncompress(fake_compressed_data, kZSTD, nullptr, raw_data.size(), uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  ret= compressor_helper.uncompress(fake_compressed_data, kZlibCompression, raw_buf, 0, uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  ret = compressor_helper.uncompress(fake_compressed_data, kLZ4Compression, raw_buf, -1, uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);
  ret = compressor_helper.uncompress(fake_compressed_data, kLZ4Compression, raw_buf, 1, uncompressed_data);
  ASSERT_EQ(Status::kInvalidArgument, ret);

  // compress and uncompress
  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    ret = compressor_helper.compress(raw_data, compressor_arr[i], compressed_data, actual_compress_type);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_EQ(compressor_arr[i], actual_compress_type);
    ASSERT_TRUE(compressed_data.size() < raw_data.size());
  
    ret = compressor_helper.uncompress(compressed_data, actual_compress_type, raw_buf, raw_data.size(), uncompressed_data);
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_TRUE(raw_data == uncompressed_data);
  }

  compressor_helper.reset();
  ASSERT_EQ(0, memory::AllocMgr::get_instance()->get_hold_size(memory::ModId::kCompressor));
}

} //namespace util
} //namespace smartengine

int main(int argc, char **argv)
{
  smartengine::util::test::remove_dir(test_dir.c_str());
  smartengine::util::Env::Default()->CreateDir(test_dir);
  std::string log_path = smartengine::util::test::TmpDir() + "/compressor_test.log";
  smartengine::logger::Logger::get_log().init(log_path.c_str(), smartengine::logger::DEBUG_LEVEL);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
