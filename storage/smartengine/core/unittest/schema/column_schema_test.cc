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

#include "schema/column_schema.h"
#include "util/testharness.h"

namespace smartengine
{
using namespace common;

namespace schema
{

TEST(ColumnSchemaTest, set_and_get)
{
  int ret = Status::kOk;
  ColumnSchema column_schema;

  // nullable
  ASSERT_FALSE(column_schema.is_nullable());
  column_schema.set_nullable();
  ASSERT_TRUE(column_schema.is_nullable());
  column_schema.unset_nullable();
  ASSERT_FALSE(column_schema.is_nullable());

  // fixed
  ASSERT_FALSE(column_schema.is_fixed());
  column_schema.set_fixed();
  ASSERT_TRUE(column_schema.is_fixed());
  column_schema.unset_fixed();
  ASSERT_FALSE(column_schema.is_fixed());

  // instant
  ASSERT_FALSE(column_schema.is_instant());
  column_schema.set_instant();
  ASSERT_TRUE(column_schema.is_instant());
  column_schema.unset_instant();
  ASSERT_FALSE(column_schema.is_instant());

  // column_count
  uint32_t column_count = UINT32_MAX;
  column_schema.set_column_count(column_count);
  column_count = column_schema.get_column_count();
  ASSERT_EQ(UINT32_MAX, column_count);
  column_schema.set_column_count(0);
  ASSERT_EQ(0, column_schema.get_column_count());

  // null_bitmap_size
  uint32_t null_bitmap_size = UINT32_MAX;
  column_schema.set_null_bitmap_size(null_bitmap_size);
  null_bitmap_size = column_schema.get_null_bitmap_size();
  ASSERT_EQ(UINT32_MAX, null_bitmap_size);
  column_schema.set_null_bitmap_size(0);
  ASSERT_EQ(0, column_schema.get_null_bitmap_size());

  // original_column_count
  uint32_t original_column_count = UINT32_MAX;
  column_schema.set_original_column_count(original_column_count);
  original_column_count = column_schema.get_original_column_count();
  ASSERT_EQ(UINT32_MAX, original_column_count);
  column_schema.set_original_column_count(0);
  ASSERT_EQ(0, column_schema.get_original_column_count());

  // original_null_bitmap_size
  uint32_t original_null_bitmap_size = UINT32_MAX;
  column_schema.set_original_null_bitmap_size(original_null_bitmap_size);
  original_null_bitmap_size = column_schema.get_original_null_bitmap_size();
  ASSERT_EQ(UINT32_MAX, original_null_bitmap_size);
  column_schema.set_original_null_bitmap_size(0);
  ASSERT_EQ(0, column_schema.get_original_null_bitmap_size());

  // null_mask
  uint8_t null_mask = UINT8_MAX;
  column_schema.set_null_mask(null_mask);
  null_mask = column_schema.get_null_mask();
  ASSERT_EQ(UINT8_MAX, null_mask);
  column_schema.set_null_mask(0);
  ASSERT_EQ(0, column_schema.get_null_mask());

  // null_offset
  uint32_t null_offset = UINT32_MAX;
  column_schema.set_null_offset(null_offset);
  null_offset = column_schema.get_null_offset();
  ASSERT_EQ(UINT32_MAX, null_offset);
  column_schema.set_null_offset(0);
  ASSERT_EQ(0, column_schema.get_null_offset());

  // data_size
  uint32_t data_size = UINT32_MAX;
  column_schema.set_data_size(data_size);
  data_size = column_schema.get_data_size();
  ASSERT_EQ(UINT32_MAX, data_size);
  column_schema.set_data_size(0);
  ASSERT_EQ(0, column_schema.get_data_size());

  // data_size_bytes
  uint32_t data_size_bytes = UINT32_MAX;
  column_schema.set_data_size_bytes(data_size_bytes);
  data_size_bytes = column_schema.get_data_size_bytes();
  ASSERT_EQ(UINT32_MAX, data_size_bytes);
  column_schema.set_data_size_bytes(0);
  ASSERT_EQ(0, column_schema.get_data_size_bytes());
}

TEST(ColumnSchemaTest, is_valid)
{
  int ret = Status::kOk;
  ColumnSchema column_schema;

  // invalid type
  column_schema.set_type(INVALID_COLUMN_TYPE);
  ASSERT_FALSE(column_schema.is_valid());
  column_schema.set_type(MAX_COLUMN_TYPE);
  ASSERT_FALSE(column_schema.is_valid());

  // invalid non-nullable data column
  column_schema.reset();
  column_schema.set_type(FIXED_COLUMN);
  column_schema.set_null_offset(1);
  // the null_offset of nullable column should be zero
  ASSERT_FALSE(column_schema.is_valid());
  column_schema.set_null_offset(0);
  column_schema.set_null_mask(1);
  // the null_mask of nullable column should be zero
  ASSERT_FALSE(column_schema.is_valid());

  // valid non-nullable data column
  column_schema.reset();
  column_schema.set_type(BLOB_COLUMN);
  ASSERT_TRUE(column_schema.is_valid());

  // invalid nullable data column
  column_schema.reset();
  column_schema.set_type(VARCHAR_COLUMN);
  column_schema.set_nullable();
  // the null_mask of nullable data column should not be zero
  ASSERT_FALSE(column_schema.is_valid());

  // valid nullable data column
  column_schema.reset();
  column_schema.set_type(PRIMARY_COLUMN);
  column_schema.set_nullable();
  column_schema.set_null_mask(1);
  ASSERT_TRUE(column_schema.is_valid());

  // invalid record header column
  column_schema.reset();
  column_schema.set_type(RECORD_HEADER);
  // the column_count of record header should be greater than zero
  ASSERT_FALSE(column_schema.is_valid());

  column_schema.reset();
  column_schema.set_type(RECORD_HEADER);
  column_schema.set_column_count(2);
  column_schema.set_null_bitmap_size(-1);
  // the null_bitmap_size of record header should be greater than or equal to zero
  ASSERT_FALSE(column_schema.is_valid());

  column_schema.reset();
  column_schema.set_type(RECORD_HEADER);
  column_schema.set_original_null_bitmap_size(1);
  // the original_null_bitmap_size of non-instant record header should be zero
  ASSERT_FALSE(column_schema.is_valid());

  column_schema.reset();
  column_schema.set_type(RECORD_HEADER);
  column_schema.set_original_column_count(1);
  // the original_column_count of non-instant record header should be zero
  ASSERT_FALSE(column_schema.is_valid());

  column_schema.reset();
  column_schema.set_type(RECORD_HEADER);
  column_schema.set_null_bitmap_size(1);
  column_schema.set_instant();
  // the original_column_count of instant record header should be larger than zero
  ASSERT_FALSE(column_schema.is_valid());

  column_schema.set_original_column_count(2);
  // the original_column_count of instant record header should less than column_count
  ASSERT_FALSE(column_schema.is_valid());


  // valid record header
  column_schema.reset();
  column_schema.set_type(RECORD_HEADER);
  column_schema.set_column_count(2);
  // valid not instant column
  ASSERT_TRUE(column_schema.is_valid());
  column_schema.set_instant();
  column_schema.set_original_column_count(1);
  // valid instant column
  ASSERT_TRUE(column_schema.is_valid());
}

TEST(ColumnSchemaTest, serialize_and_deserialize)
{
  int ret = Status::kOk;
  ColumnSchema column_schema;
  char *serialize_buf = nullptr;
  int64_t serialize_size = 0;
  int64_t pos = 0;

  column_schema.set_type(FIXED_COLUMN);
  column_schema.set_fixed();
  column_schema.set_nullable();
  column_schema.set_instant();
  column_schema.set_column_count(5);
  column_schema.set_null_bitmap_size(6);
  column_schema.set_original_column_count(7);
  column_schema.set_original_null_bitmap_size(8);
  column_schema.set_null_mask(128);
  column_schema.set_null_offset(4);
  column_schema.set_data_size(4);

  serialize_size = column_schema.get_serialize_size();
  serialize_buf = new char [serialize_size];
  ASSERT_TRUE(nullptr != serialize_buf);

  // serialize
  ret = column_schema.serialize(serialize_buf, serialize_size, pos);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_EQ(serialize_size, pos);

  //deserialize
  ColumnSchema des_column_schema;
  pos = 0;
  ret = des_column_schema.deserialize(serialize_buf, serialize_size, pos);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_EQ(serialize_size, pos);
  ASSERT_TRUE(des_column_schema.is_fixed());
  ASSERT_TRUE(des_column_schema.is_nullable());
  ASSERT_TRUE(des_column_schema.is_instant());
  ASSERT_EQ(column_schema.get_column_count(), des_column_schema.get_column_count());
  ASSERT_EQ(column_schema.get_null_bitmap_size(), des_column_schema.get_null_bitmap_size());
  ASSERT_EQ(column_schema.get_original_column_count(), des_column_schema.get_original_column_count());
  ASSERT_EQ(column_schema.get_original_null_bitmap_size(), des_column_schema.get_original_null_bitmap_size());
  ASSERT_EQ(column_schema.get_null_mask(), des_column_schema.get_null_mask());
  ASSERT_EQ(column_schema.get_null_offset(), des_column_schema.get_null_offset());
  ASSERT_EQ(column_schema.get_data_size(), des_column_schema.get_data_size());
}

} // namespace schema
} // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}