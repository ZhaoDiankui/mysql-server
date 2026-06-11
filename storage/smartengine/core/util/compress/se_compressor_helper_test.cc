#include "util/testharness.h"
#include "util/testutil.h"
#include "se_compressor_helper.h"

static const std::string test_dir = smartengine::util::test::TmpDir() + "/se_compressor_helper_test";
namespace smartengine {
using namespace common;
using namespace table;

namespace util {
class SeCompressorHelperTest : public testing::Test
{
protected:
  static const int64_t COMPRESSOR_TYPE_COUNT = 3;
  static CompressionType compressor_arr[COMPRESSOR_TYPE_COUNT];
};
CompressionType SeCompressorHelperTest::compressor_arr[SeCompressorHelperTest::COMPRESSOR_TYPE_COUNT]
    = {kLZ4Compression, kZlibCompression, kZSTD};

TEST_F(SeCompressorHelperTest, init)
{
  int ret = Status::kOk;
  SeCompressorHelper compressor_helper;

  //Not supported compression type
  ret = compressor_helper.init(kDisableCompressionOption);
  ASSERT_EQ(Status::kNotSupported, ret);

  //Supported compression type
  ret = compressor_helper.init(kZSTD);
  ASSERT_EQ(Status::kOk, ret);

  //Init twice
  ret = compressor_helper.init(kZSTD);
  ASSERT_EQ(Status::kInitTwice, ret);
}

TEST_F(SeCompressorHelperTest, compress)
{
  int ret = Status::kOk;  
  const char *short_text = "wesql-smartengine-wesql-smartengine" ;
  const int64_t short_text_size = strlen(short_text);

  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    SeCompressorHelper compressor_helper;
    char *compressed_data = nullptr;
    int64_t compressed_data_size = 0;
    CompressionType compress_type = kNoCompression;
    
    ret = compressor_helper.init(compressor_arr[i]);
    ASSERT_EQ(Status::kOk, ret);

    ret = compressor_helper.compress(short_text,
                                     short_text_size,
                                     compressed_data,
                                     compressed_data_size,
                                     compress_type);
   
    ASSERT_EQ(Status::kOk, ret);
    ASSERT_TRUE(nullptr != compressed_data);
    ASSERT_TRUE(compressed_data_size > 0);
    ASSERT_EQ(compressor_arr[i], compress_type);
  }
}

TEST_F(SeCompressorHelperTest, not_compress)
{
  int ret = Status::kOk;  
  const char *short_text = "123456" ;
  const int64_t short_text_size = strlen(short_text);

  for (int64_t i = 0; i < COMPRESSOR_TYPE_COUNT; ++i) {
    SeCompressorHelper compressor_helper;
    char *compressed_data = nullptr;
    int64_t compressed_data_size = 0;
    CompressionType compress_type = kNoCompression;
    
    ret = compressor_helper.init(compressor_arr[i]);
    ASSERT_EQ(Status::kOk, ret);

    ret = compressor_helper.compress(short_text,
                                     short_text_size,
                                     compressed_data,
                                     compressed_data_size,
                                     compress_type);
   
    ASSERT_EQ(Status::kNotCompress, ret);
    ASSERT_TRUE(nullptr != compressed_data);
    ASSERT_TRUE(compressed_data_size > 0);
    ASSERT_EQ(kNoCompression, compress_type);
  }
}
} // namespace util
} // namespace smartengine

int main(int argc, char **argv)
{
  smartengine::util::test::remove_dir(test_dir.c_str());
  smartengine::util::Env::Default()->CreateDir(test_dir);
  std::string log_path = smartengine::util::test::TmpDir() + "/se_compressor_helper_test.log";
  smartengine::logger::Logger::get_log().init(log_path.c_str(), smartengine::logger::DEBUG_LEVEL);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
