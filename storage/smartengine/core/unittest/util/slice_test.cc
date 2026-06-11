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

#include "util/slice.h"
#include "util/testharness.h"
#include <cstdio>
#include <cstring>

namespace smartengine
{
using namespace common;
namespace util
{

TEST(Slice, equal)
{
  int ret = Status::kOk;
  const int64_t BUF_LENGTH = 3;
  const char *str = "zds";
  char *buf1 = new char[BUF_LENGTH];
  char *buf2 = new char[BUF_LENGTH];
  memcpy(buf1, str, BUF_LENGTH);
  memcpy(buf2, str, BUF_LENGTH);
  Slice slice1(buf1, BUF_LENGTH);
  Slice slice2(buf2, BUF_LENGTH);
  fprintf(stderr, "data1= %p, data2= %p\n", slice1.data(), slice2.data());

  ASSERT_TRUE(slice1 == slice2);
}

} // namespace util
} // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}
