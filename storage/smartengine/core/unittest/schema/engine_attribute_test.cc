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

#include "schema/engine_attribute.h"
#include "util/testharness.h"

namespace smartengine
{
using namespace common;

namespace schema
{

TEST(EngineAttributeTest, is_valid)
{
  EngineAttribute engine_attribute;

  // the initialize EngineAttribute is valid
  ASSERT_TRUE(engine_attribute.is_valid());

  // invalid block size
  engine_attribute.set_block_size(0);
  ASSERT_FALSE(engine_attribute.is_valid());
  engine_attribute.set_block_size(1024 * 1024 + 1);
  ASSERT_FALSE(engine_attribute.is_valid());
}

TEST(EngineAttributeTest, serialize_and_deserialize)
{
  int ret = Status::kOk;
  EngineAttribute engine_attribute;
  char *serialize_buf = nullptr;
  int64_t serialize_size = 0;
  int64_t pos = 0;

  serialize_size = engine_attribute.get_serialize_size();
  serialize_buf = new char [serialize_size];
  ASSERT_TRUE(nullptr != serialize_buf);

  // serialize
  engine_attribute.set_block_size(123);
  ret = engine_attribute.serialize(serialize_buf, serialize_size, pos);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_EQ(serialize_size, pos);

  // deserialize
  EngineAttribute des_engine_attribute;
  pos = 0;
  ret = des_engine_attribute.deserialize(serialize_buf, serialize_size, pos); 
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_EQ(serialize_size, pos);

  // check
  ASSERT_FALSE(engine_attribute.use_column_format());
  ASSERT_EQ(engine_attribute.use_column_format(), des_engine_attribute.use_column_format());
  ASSERT_TRUE(engine_attribute.use_bloom_filter());
  ASSERT_EQ(engine_attribute.use_bloom_filter(), des_engine_attribute.use_bloom_filter());
  ASSERT_EQ(123, engine_attribute.get_block_size());
  ASSERT_EQ(engine_attribute.get_block_size(), des_engine_attribute.get_block_size());
  ASSERT_EQ(1, engine_attribute.get_compress_types().size());
  ASSERT_EQ(kZSTD, engine_attribute.get_compress_types().at(0));
  ASSERT_EQ(engine_attribute.get_compress_types().size(), des_engine_attribute.get_compress_types().size());
}

} // namespace schema
} // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}
