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

namespace smartengine
{
namespace schema
{
TableSchema::TableSchema()
    : databae_name_(),
      primary_index_id_(0),
      index_id_(0),
      attr_(0),
      packed_column_count_(0),
      engine_attribute_(),
      column_schemas_()
{}

TableSchema::TableSchema(const TableSchema &table_schema)
    : databae_name_(table_schema.databae_name_),
      primary_index_id_(table_schema.primary_index_id_),
      index_id_(table_schema.index_id_),
      attr_(table_schema.attr_),
      packed_column_count_(table_schema.packed_column_count_),
      engine_attribute_(table_schema.engine_attribute_),
      column_schemas_(table_schema.column_schemas_)
{}

TableSchema &TableSchema::operator=(const TableSchema &table_schema)
{
  databae_name_ = table_schema.databae_name_;
  primary_index_id_ = table_schema.primary_index_id_;
  index_id_ = table_schema.index_id_;
  attr_ = table_schema.attr_;
  packed_column_count_ = table_schema.packed_column_count_;
  engine_attribute_ = table_schema.engine_attribute_;
  column_schemas_ = table_schema.column_schemas_;

  return *this;
}

TableSchema::~TableSchema()
{}

void TableSchema::reset()
{
  databae_name_.clear();
  primary_index_id_ = 0;
  index_id_ = 0;
  attr_ = 0;
  packed_column_count_ = 0;
  engine_attribute_.reset();
  column_schemas_.clear();
}

bool TableSchema::is_valid() const
{
  bool res = index_id_ >= 0 && engine_attribute_.is_valid();
  //TODO:Zhao Dongsheng, the hard code "1" should optimize
  //Check table schema's validation except system and default subtable
  if (res && primary_index_id_ > 1) {
    res = (column_schemas_.size() >= 2);
    for (uint32_t i = 0; res && i < column_schemas_.size(); ++i) {
      res = (res && column_schemas_[i].is_valid());
    }
  }

  return res;
}

DEFINE_TO_STRING(TableSchema, KV_(databae_name), KV_(primary_index_id), 
    KV_(index_id), KV_(attr), KV_(packed_column_count), KV_(engine_attribute),
    KV_(column_schemas))

DEFINE_COMPACTIPLE_SERIALIZATION(TableSchema, databae_name_, primary_index_id_,
    index_id_, attr_, packed_column_count_, engine_attribute_, column_schemas_)

} // namespace schema
} // namespace smartengine