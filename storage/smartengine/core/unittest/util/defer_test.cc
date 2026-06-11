#include "util/defer.h"
#include <gtest/gtest.h>
#include "util/testharness.h"

namespace smartengine {
namespace util {
TEST(DEFER, DEFER_NORMAL) {
  int value = 0;
  {
    auto d = defer([&value] { value = 1; });
  }
  ASSERT_EQ(1, value);
}
}  // namespace util
}  // namespace smartengine

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}