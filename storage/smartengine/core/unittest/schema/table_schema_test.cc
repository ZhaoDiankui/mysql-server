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

#include "schema/table_schema.h"
#include "util/testharness.h"

namespace smartengine
{
using namespace common;

namespace schema
{

TEST(TableSchemaTest, is_valid)
{
  int ret = Status::kOk;
  ColumnSchema valid_column_schema;
  valid_column_schema.set_type(BLOB_COLUMN);
  ASSERT_TRUE(valid_column_schema.is_valid());
  ColumnSchema invalid_column_schema;
  invalid_column_schema.set_type(BLOB_COLUMN);
  invalid_column_schema.set_nullable();
  ASSERT_FALSE(invalid_column_schema.is_valid());
  TableSchema table_schema;

  // the initialize TableSchema is valid
  ASSERT_TRUE(table_schema.is_valid());

  // invalid engine_attribute.
  table_schema.reset();
  table_schema.get_engine_attribute().set_block_size(-1);
  ASSERT_FALSE(table_schema.is_valid());
  table_schema.reset();
  table_schema.get_engine_attribute().set_block_size(-1);
  table_schema.set_primary_index_id(2);
  table_schema.get_column_schemas().push_back(valid_column_schema);
  table_schema.get_column_schemas().push_back(valid_column_schema);
  ASSERT_FALSE(table_schema.is_valid());

  // invalid column schema count
  table_schema.reset();
  table_schema.set_primary_index_id(2);
  ASSERT_FALSE(table_schema.is_valid());

  // invalid column_schema
  table_schema.reset();
  table_schema.set_primary_index_id(2);
  table_schema.get_column_schemas().push_back(invalid_column_schema);
  table_schema.get_column_schemas().push_back(valid_column_schema);
  ASSERT_FALSE(table_schema.is_valid());
  table_schema.reset();
  table_schema.set_primary_index_id(2);
  table_schema.get_column_schemas().push_back(valid_column_schema);
  table_schema.get_column_schemas().push_back(invalid_column_schema);
  ASSERT_FALSE(table_schema.is_valid());

  //valid table schema
  table_schema.reset();
  table_schema.set_primary_index_id(2);
  table_schema.get_column_schemas().push_back(valid_column_schema);
  table_schema.get_column_schemas().push_back(valid_column_schema);
  ASSERT_TRUE(table_schema.is_valid());
}

TEST(TableSchemaTest, serialize_and_deserialize)
{
  int ret = Status::kOk;
  TableSchema table_schema;
  char *serialize_buf = nullptr;
  int64_t serialize_size = 0;
  int64_t pos = 0;

  ColumnSchema pk_column;
  pk_column.set_type(PRIMARY_COLUMN);
  pk_column.set_fixed();
  pk_column.set_data_size(4);

  ColumnSchema rec_hdr_column;
  rec_hdr_column.set_type(RECORD_HEADER);
  rec_hdr_column.set_instant();
  rec_hdr_column.set_column_count(4);
  rec_hdr_column.set_null_bitmap_size(2);
  rec_hdr_column.set_original_column_count(3);
  rec_hdr_column.set_original_null_bitmap_size(1);
  rec_hdr_column.set_null_mask(64);
  rec_hdr_column.set_null_offset(1);
  rec_hdr_column.set_data_size(4);

  ColumnSchema data_column;
  uint8_t null_mask = 255;
  data_column.set_type(FIXED_COLUMN);
  data_column.set_fixed();
  data_column.set_nullable();
  data_column.set_null_mask(null_mask);
  data_column.set_null_offset(4);
  data_column.set_data_size(4);

  table_schema.set_index_id(123);
  table_schema.set_primary_index_id(124);
  table_schema.set_unpack();
  table_schema.set_packed_column_count(2);
  table_schema.get_column_schemas().push_back(pk_column);
  table_schema.get_column_schemas().push_back(rec_hdr_column);
  table_schema.get_column_schemas().push_back(data_column);


  serialize_size = table_schema.get_serialize_size();
  serialize_buf = new char [serialize_size];
  ASSERT_TRUE(nullptr != serialize_buf);

  // serialize
  ret = table_schema.serialize(serialize_buf, serialize_size, pos);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_EQ(serialize_size, pos);

  //deserialize
  TableSchema des_table_schema;
  pos = 0;
  ret = des_table_schema.deserialize(serialize_buf, serialize_size, pos);
  ASSERT_EQ(Status::kOk, ret);
  ASSERT_EQ(serialize_size, pos);

  // check
  ASSERT_EQ(123, table_schema.get_index_id());
  ASSERT_EQ(table_schema.get_index_id(), des_table_schema.get_index_id());
  ASSERT_EQ(124, table_schema.get_primary_index_id());
  ASSERT_EQ(table_schema.get_primary_index_id(), des_table_schema.get_primary_index_id());
  ASSERT_TRUE(table_schema.has_unpack());
  ASSERT_TRUE(des_table_schema.has_unpack());
  ASSERT_EQ(2, table_schema.get_packed_column_count());
  ASSERT_EQ(table_schema.get_packed_column_count(), des_table_schema.get_packed_column_count());

  ASSERT_TRUE(table_schema.get_column_schemas()[0].is_primary());
  ASSERT_TRUE(table_schema.get_column_schemas()[0].is_fixed());
  ASSERT_TRUE(des_table_schema.get_column_schemas()[0].is_primary());
  ASSERT_TRUE(des_table_schema.get_column_schemas()[0].is_fixed());
  ASSERT_EQ(4, table_schema.get_column_schemas()[0].get_data_size());
  ASSERT_EQ(table_schema.get_column_schemas()[0].get_data_size(), des_table_schema.get_column_schemas()[0].get_data_size());

  ASSERT_EQ(RECORD_HEADER, table_schema.get_column_schemas()[1].get_type());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_type(), des_table_schema.get_column_schemas()[1].get_type());
  ASSERT_TRUE(table_schema.get_column_schemas()[1].is_instant());
  ASSERT_TRUE(des_table_schema.get_column_schemas()[1].is_instant());
  ASSERT_EQ(4, table_schema.get_column_schemas()[1].get_column_count());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_column_count(), des_table_schema.get_column_schemas()[1].get_column_count());
  ASSERT_EQ(2, table_schema.get_column_schemas()[1].get_null_bitmap_size());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_null_bitmap_size(), des_table_schema.get_column_schemas()[1].get_null_bitmap_size());
  ASSERT_EQ(3, table_schema.get_column_schemas()[1].get_original_column_count());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_original_column_count(), des_table_schema.get_column_schemas()[1].get_original_column_count());
  ASSERT_EQ(1, table_schema.get_column_schemas()[1].get_original_null_bitmap_size());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_original_null_bitmap_size(), des_table_schema.get_column_schemas()[1].get_original_null_bitmap_size());
  ASSERT_EQ(64, table_schema.get_column_schemas()[1].get_null_mask());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_null_mask(), des_table_schema.get_column_schemas()[1].get_null_mask());
  ASSERT_EQ(1, table_schema.get_column_schemas()[1].get_null_offset());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_null_offset(), des_table_schema.get_column_schemas()[1].get_null_offset());
  ASSERT_EQ(4, table_schema.get_column_schemas()[1].get_data_size());
  ASSERT_EQ(table_schema.get_column_schemas()[1].get_data_size(), des_table_schema.get_column_schemas()[1].get_data_size());

  ASSERT_EQ(FIXED_COLUMN, table_schema.get_column_schemas()[2].get_type());
  ASSERT_EQ(table_schema.get_column_schemas()[2].get_type(), des_table_schema.get_column_schemas()[2].get_type());
  ASSERT_TRUE(table_schema.get_column_schemas()[2].is_fixed());
  ASSERT_TRUE(table_schema.get_column_schemas()[2].is_nullable());
  ASSERT_TRUE(des_table_schema.get_column_schemas()[2].is_fixed());
  ASSERT_TRUE(des_table_schema.get_column_schemas()[2].is_nullable());
  null_mask = table_schema.get_column_schemas()[2].get_null_mask(); 
  ASSERT_EQ(255, null_mask);
  ASSERT_EQ(table_schema.get_column_schemas()[2].get_null_mask(), des_table_schema.get_column_schemas()[2].get_null_mask());
  ASSERT_EQ(4, table_schema.get_column_schemas()[2].get_null_offset());
  ASSERT_EQ(table_schema.get_column_schemas()[2].get_null_offset(), des_table_schema.get_column_schemas()[2].get_null_offset());
  ASSERT_EQ(4, table_schema.get_column_schemas()[2].get_data_size());
  ASSERT_EQ(table_schema.get_column_schemas()[2].get_data_size(), des_table_schema.get_column_schemas()[2].get_data_size());
}

} // namespace schema
} // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}