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

#include "util/serialization.h"
#include "util/to_string.h"
#include <cstdint>

namespace smartengine
{
namespace schema
{

enum ColumnType
{
  INVALID_COLUMN_TYPE = 0,
  PRIMARY_COLUMN = 1,
  RECORD_HEADER = 2,
  NULL_BITMAP = 3,
  UNPACK_INFO = 4,
  BLOB_COLUMN = 5,
  VARCHAR_COLUMN = 6,
  STRING_COLUMN = 7,
  FIXED_COLUMN = 8,
  MAX_COLUMN_TYPE,
};

inline bool is_data_column(ColumnType type)
{
  return PRIMARY_COLUMN == type ||
         BLOB_COLUMN == type ||
         VARCHAR_COLUMN == type ||
         STRING_COLUMN == type ||
         FIXED_COLUMN == type;
}

class ColumnSchema
{
public:
  ColumnSchema();
  ColumnSchema(const ColumnSchema &column_schema);
  ColumnSchema &operator=(const ColumnSchema &column_schema);
  ~ColumnSchema();

  void reset();
  bool is_valid() const;
  void set_nullable() { attr_ |= FLAG_MASK_NULLABLE; }
  void unset_nullable() { attr_ &= (~FLAG_MASK_NULLABLE); }
  bool is_nullable() const { return 0 != (attr_ & FLAG_MASK_NULLABLE); }
  void set_fixed() { attr_ |= FLAG_MASK_FIXED; }
  void unset_fixed() { attr_ &= (~FLAG_MASK_FIXED); }
  bool is_fixed() const { return 0 != (attr_ & FLAG_MASK_FIXED); }
  void set_instant() {attr_ |= FLAG_MASK_INSTANT; }
  void unset_instant() { attr_ &= (~FLAG_MASK_INSTANT); }
  bool is_instant() const { return 0 != (attr_ & FLAG_MASK_INSTANT); }
  bool is_primary() const { return PRIMARY_COLUMN == type_; }
  void set_type(ColumnType type) { type_ = type; }
  ColumnType get_type() const { return type_; }
  void set_column_count(int32_t column_count) { column_count_ = column_count; }
  int32_t get_column_count() const { return column_count_; }
  void set_null_bitmap_size(int32_t null_bitmap_size) { null_bitmap_size_ = null_bitmap_size; }
  int32_t get_null_bitmap_size() const { return null_bitmap_size_; }
  void set_original_column_count(int32_t original_column_count) { original_column_count_ = original_column_count; }
  int32_t get_original_column_count() const { return original_column_count_; }
  void set_original_null_bitmap_size(int32_t original_null_bitmap_size) { original_null_bitmap_size_ = original_null_bitmap_size; }
  int32_t get_original_null_bitmap_size() const { return original_null_bitmap_size_; }
  void set_null_mask(uint8_t null_mask) { null_mask_ = null_mask; }
  uint8_t get_null_mask() const { return null_mask_; }
  void set_null_offset(int32_t null_offset) { null_offset_ = null_offset; }
  int32_t get_null_offset() const { return null_offset_; }
  void set_data_size(int32_t data_size) { data_size_ = data_size; }
  int32_t get_data_size() const { return data_size_; }
  void set_data_size_bytes(int32_t data_size_bytes) { data_size_bytes_ = data_size_bytes; }
  int32_t get_data_size_bytes() const { return data_size_bytes_; }

  static const int64_t COLUMN_SCHEMA_VERSION = 1;
  DECLARE_SERIALIZATION()
  DECLARE_TO_STRING()

private:
  static const int8_t FLAG_MASK_NULLABLE = 0x01;
  static const int8_t FLAG_MASK_FIXED = 0x02;
  static const int8_t FLAG_MASK_INSTANT = 0x04;

private:
  ColumnType type_;

  /**column attributes flag:
  +----------+------------------------------------+
  | nullable |  fixed  | instant  | reserved bits |
  |  (bit 0) | (bit 1) |  (bit 2) |  (bit 3~7)    |
  +----------+---------+--------------------------+
  */
  int8_t attr_;

  /**Specially for record header*/
  int32_t column_count_;
  int32_t null_bitmap_size_;
  int32_t original_column_count_;
  int32_t original_null_bitmap_size_;

  /**Specially for data column*/
  int8_t null_mask_;
  int32_t null_offset_;

  // data size can be zero, like binary(0).
  union {
    int32_t data_size_;
    int32_t data_size_bytes_;
  };
};
typedef std::vector<ColumnSchema> ColumnSchemaArray;

} // namespace schema
} // namespace smartengine