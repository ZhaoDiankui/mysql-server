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
#include "schema/table_schema.h"
#include "table/block_struct.h"
#include "table/block_writer.h"
#include "table/column_struct.h"
#include "table/column_unit.h"

namespace smartengine
{
namespace table
{

class ColumnBlockWriter : public BlockWriter
{
public:
  ColumnBlockWriter();
  virtual ~ColumnBlockWriter() override;

  int init(const schema::TableSchema &table_schema, common::CompressionType compress_type);
  void destroy();
  virtual void reuse() override;
  virtual int append(const common::Slice &key, const common::Slice &value) override;
  virtual int build(common::Slice &block_data, BlockInfo &block_info) override;
  virtual int64_t current_size() const override;
  virtual int64_t future_size(const uint32_t key_size, const uint32_t value_size) const override;
  virtual bool is_empty() const override { return (0 == row_count_); }

private:
  int init_columns(const schema::ColumnSchemaArray &column_schemas);
  int init_column_unit_writers(const schema::ColumnSchemaArray &col_schemas, const common::CompressionType compress_type);
  int convert_to_columns(const common::Slice &key, const common::Slice &value, ColumnArray &columns);
  int write_columns(const ColumnArray &columns);

private:
  static const int64_t DEFAULT_BLOCK_BUFFER_SIZE = 16 * 1024; //16KB

private:
  bool is_inited_;
  schema::TableSchema table_schema_;
  common::CompressionType compress_type_;
  ColumnParseCtx parse_ctx_;
  ColumnArray columns_;
  ColumnUnitWriterArray column_unit_writers_;
  int64_t row_count_;
  int64_t column_count_;
  util::AutoBufferWriter block_buf_;
};

} // namespace table
} // namespace smartengine