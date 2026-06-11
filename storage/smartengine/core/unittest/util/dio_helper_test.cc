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

#include "util/dio_helper.h"
#include "util/testharness.h"
#include "util/testutil.h"
#include <random>

namespace smartengine
{
namespace util
{

TEST(dio_helper, is_aligned)
{
  // Aligned number
  ASSERT_TRUE(DIOHelper::is_aligned(0));
  ASSERT_TRUE(DIOHelper::is_aligned(DIOHelper::DIO_ALIGN_SIZE));
  ASSERT_TRUE(DIOHelper::is_aligned(DIOHelper::DIO_ALIGN_SIZE * 2));

  // Not aligned number
  ASSERT_FALSE(DIOHelper::is_aligned(DIOHelper::DIO_ALIGN_SIZE - 1));
  ASSERT_FALSE(DIOHelper::is_aligned(DIOHelper::DIO_ALIGN_SIZE + 1));
  ASSERT_FALSE(DIOHelper::is_aligned(DIOHelper::DIO_ALIGN_SIZE * 2 - 1));
  ASSERT_FALSE(DIOHelper::is_aligned(DIOHelper::DIO_ALIGN_SIZE * 2 + 1));

  // Random number in [1, DIOHelper::DIO_ALIGN_SIZE - 1]
  {
    test::RandomInt64Generator generator(1, DIOHelper::DIO_ALIGN_SIZE - 1);
    for(int32_t i = 0; i < DIOHelper::DIO_ALIGN_SIZE; ++i) {
      ASSERT_FALSE(DIOHelper::is_aligned(generator.generate()));
    }
  }

  // Random number in [DIOHelper::DIO_ALIGN_SIZE + 1, DIOHelper::DIO_ALIGN_SIZE * 2 - 1)
  {
    test::RandomInt64Generator generator(DIOHelper::DIO_ALIGN_SIZE + 1, DIOHelper::DIO_ALIGN_SIZE * 2 - 1);
    for(int32_t i = 0; i < DIOHelper::DIO_ALIGN_SIZE; ++i) {
      ASSERT_FALSE(DIOHelper::is_aligned(generator.generate()));
    }
  }
}

TEST(dio_helper, align_offset)
{
  // Special boundary condition
  ASSERT_EQ(0, DIOHelper::align_offset(0));
  ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE, DIOHelper::align_offset(DIOHelper::DIO_ALIGN_SIZE));
  ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE * 2, DIOHelper::align_offset(DIOHelper::DIO_ALIGN_SIZE * 2));

  // Random offset between [1, DIOHelper::DIO_ALIGN_SIZE - 1]
  {
    test::RandomInt64Generator generator(1, DIOHelper::DIO_ALIGN_SIZE - 1);
    for(int32_t i = 0; i < DIOHelper::DIO_ALIGN_SIZE; ++i) {
      ASSERT_EQ(0, DIOHelper::align_offset(generator.generate()));
    }
  }

  // Random number in [DIOHelper::DIO_ALIGN_SIZE + 1, DIOHelper::DIO_ALIGN_SIZE - 1)
  {
    test::RandomInt64Generator generator(DIOHelper::DIO_ALIGN_SIZE + 1, DIOHelper::DIO_ALIGN_SIZE * 2 - 1);
    for(int32_t i = 0; i < DIOHelper::DIO_ALIGN_SIZE; ++i) {
      ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE, DIOHelper::align_offset(generator.generate()));
    }
  }
}

TEST(dio_helper, align_size)
{
  // special boundary condition
  ASSERT_EQ(0, DIOHelper::align_size(0));
  ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE, DIOHelper::align_size(DIOHelper::DIO_ALIGN_SIZE));
  ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE * 2, DIOHelper::align_size(DIOHelper::DIO_ALIGN_SIZE * 2));

  //Random size between [1, DIOHelper::DIO_ALIGN_SIZE - 1]
  {
    test::RandomInt64Generator generator(1, DIOHelper::DIO_ALIGN_SIZE - 1);
    for(int32_t i = 0; i < DIOHelper::DIO_ALIGN_SIZE; ++i) {
      ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE, DIOHelper::align_size(generator.generate()));
    }    
  } 

  // Random number in [DIOHelper::DIO_ALIGN_SIZE + 1, DIOHelper::DIO_ALIGN_SIZE - 1)
  {
    test::RandomInt64Generator generator(DIOHelper::DIO_ALIGN_SIZE + 1, DIOHelper::DIO_ALIGN_SIZE * 2 - 1);
    for(int32_t i = 0; i < DIOHelper::DIO_ALIGN_SIZE; ++i) {
      ASSERT_EQ(DIOHelper::DIO_ALIGN_SIZE * 2, DIOHelper::align_size(generator.generate()));
    }
  }
}

} // namespace test
} // namespace smartengine

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}