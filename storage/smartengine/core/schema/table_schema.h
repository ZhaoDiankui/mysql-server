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

#pragma once

#include "schema/column_schema.h"
#include "schema/engine_attribute.h"

namespace smartengine
{
namespace schema
{

class TableSchema
{
public:
  TableSchema();
  TableSchema(const TableSchema &table_schema);
  TableSchema &operator=(const TableSchema &table_schema);
  ~TableSchema();

  void reset();
  bool is_valid() const;
  void set_unpack() { attr_ |= FLAG_MASK_UNPACK; }
  void unset_unpack() { attr_ &= (~FLAG_MASK_UNPACK); }
  bool has_unpack() const { return 0 != (attr_ & FLAG_MASK_UNPACK); }
  bool use_column_format() const { return (primary_index_id_ == index_id_) && engine_attribute_.use_column_format(); }
  void set_database_name(const std::string &database_name) { databae_name_ = database_name; }
  std::string get_database_name() const { return databae_name_; }
  void set_primary_index_id(int64_t primary_index_id) { primary_index_id_ = primary_index_id; }
  int64_t get_primary_index_id() const { return primary_index_id_; }
  void set_index_id(int64_t index_id) { index_id_ = index_id; }
  int64_t get_index_id() const { return index_id_; }
  void set_packed_column_count(uint32_t packed_column_count) { packed_column_count_ = packed_column_count; }
  uint32_t get_packed_column_count() const { return packed_column_count_; }
  EngineAttribute &get_engine_attribute() { return engine_attribute_; }
  const EngineAttribute &get_engine_attribute() const { return engine_attribute_; }
  ColumnSchemaArray &get_column_schemas() { return column_schemas_; }
  const ColumnSchemaArray &get_column_schemas() const { return column_schemas_; }
  int32_t get_block_size() const { return engine_attribute_.get_block_size(); }

  static const int64_t TABLE_SCHEMA_VERSION = 1;
  DECLARE_COMPACTIPLE_SERIALIZATION(TABLE_SCHEMA_VERSION)
  DECLARE_TO_STRING()

private:
  static const uint8_t FLAG_MASK_UNPACK = 0x01;

private:
  std::string databae_name_;
  int64_t primary_index_id_;
  int64_t index_id_;
  /**table attributes flag:
  +-----------------+--------------------------+----------------+
  | has unpack info | has instant added column |  reserved bits |
  |     (bit 0)     |        (bit 1)           |   (bit 2~7)    |
  +-----------------+--------------------------+----------------+
  */
  int8_t attr_;
  /**The count of columns which have been packed to memory comparison format.
  The primary key needs to be assembled in a format suitable for memory comparison,
  which may include unpack information or not.Therefore, the parsing of the primary
  key requires the use of unpack function and unpack information, making it distinct
  from non-primary columns.*/
  uint32_t packed_column_count_;
  EngineAttribute engine_attribute_;
  /**The table schema typically includes at least the following types of columns:
  1.PRIMARY_COLUMN, which represents all columns in primary key.
  2.RECORD_HEADER
  And optionaly includes following types of columns:
  3. NULL_BITMAP, if there has nullable column.
  4. UNPACK_INFO, for some charset.
  5. BLOB_COLUMN, VARCHAR_COLUMN, STRING_COLUMN, FIXED_COLUMN.*/
  ColumnSchemaArray column_schemas_;
};

} // namespace schema
} // namespace smartengine