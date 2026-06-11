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

#include "options/advanced_options.h"
#include "table/column_struct.h"
#include "util/compress/compressor_helper.h"
#include "util/data_buffer.h"

namespace smartengine
{
namespace table
{
struct ColumnParseCtx;

struct ColumnUnitInfo
{
  static const int64_t COLUMN_UNIT_INFO_VERSION = 1;

  int16_t column_type_;
  int8_t compress_type_;
  int32_t column_count_;
  int32_t null_column_count_;
  int32_t data_offset_;
  int32_t data_size_;
  int32_t raw_data_size_;

  ColumnUnitInfo();
  ColumnUnitInfo(const ColumnUnitInfo &column_unit_info);
  ColumnUnitInfo &operator=(const ColumnUnitInfo &column_unit_info);
  ~ColumnUnitInfo();

  void reset();
  bool is_valid() const;
  DECLARE_TO_STRING()
  DECLARE_COMPACTIPLE_SERIALIZATION(COLUMN_UNIT_INFO_VERSION)
};

class ColumnUnitWriter
{
public:
  ColumnUnitWriter();
  ~ColumnUnitWriter();

  int init(const schema::ColumnSchema &col_schema, const common::CompressionType compress_type);
  void reuse();
  int append(const Column *column);
  int build(common::Slice &unit_data, ColumnUnitInfo &unit_info);
  int64_t current_size() const;

private:
  int write_primary_column(const Column *column);
  int write_normal_column(const Column *column);

private:
  static const int64_t DEFAULT_COLUMN_UNIT_BUFFER_SIZE = 16 * 1024; //16KB

private:
  bool is_inited_;
  schema::ColumnSchema column_schema_;
  common::CompressionType compress_type_;
  util::CompressorHelper compressor_helper_;
  int32_t column_count_;
  int32_t null_column_count_;
  util::AutoBufferWriter buf_;
};
typedef std::vector<ColumnUnitWriter *> ColumnUnitWriterArray;

class ColumnUnitReader
{
public:
  ColumnUnitReader();
  ~ColumnUnitReader();

  // TODO(Zhao Dongsheng) : store ColumnSchema in ColumnUnitInfo?
  int init(const common::Slice &unit_data,
           const ColumnUnitInfo &unit_info,
           const schema::ColumnSchema &column_schema,
           int64_t column_count);
  void destroy();
  int get_next_column(ColumnParseCtx &parse_ctx, Column *&column);
  bool reach_end() const { return column_count_ == column_cursor_; }

private:
  bool is_inited_;
  char *raw_unit_buf_; // buffer to store uncompressed unit data
  common::Slice raw_unit_data_;
  int64_t column_count_;
  int64_t column_cursor_;
  int64_t pos_;
  schema::ColumnSchema column_schema_;
  Column *column_;
};
typedef std::vector<ColumnUnitReader *> ColumnUnitReaderArray; 

} // namespace table
} // namespace smartengine