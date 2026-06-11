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

#include "schema/table_schema.h"
#include "table/column_struct.h"
#include "table/column_unit.h"

namespace smartengine
{
using namespace schema;

namespace table
{
struct BlockInfo;

class ColumnBlockIterator
{
public:
  ColumnBlockIterator();
  ~ColumnBlockIterator();

  int init(const common::Slice &block, const BlockInfo &block_info, const TableSchema &table_schema);
  void destroy();
  int get_next_row(common::Slice &key, common::Slice &value);

private:
  int init_column_unit_readers(const common::Slice &block_data,
                               const std::vector<ColumnUnitInfo> &unit_infos,
                               const ColumnSchemaArray &column_schemas,
                               int64_t column_count);
  int get_next_columns(ColumnArray &columns);
  int transform_to_row(const ColumnArray &columns);

private:
  bool is_inited_;
  const char *block_data_;
  int64_t block_size_;
  int64_t row_count_;
  int64_t row_cursor_;
  int64_t pos_;
  TableSchema table_schema_;
  ColumnParseCtx parse_ctx_;
  ColumnUnitReaderArray readers_;
  util::AutoBufferWriter key_;
  util::AutoBufferWriter value_;
};

} // namespace table
} // namespace smartengine