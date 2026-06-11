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

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include "options/advanced_options.h"
#include "util/serialization.h"
#include "util/to_string.h"

namespace smartengine
{
namespace schema
{

class EngineAttribute
{
public:
  EngineAttribute();
  ~EngineAttribute();

  void reset();
  bool is_valid() const;
  int parse(const std::string &attributes_json);
  bool use_column_format() const { return COLUMNAR_FORMAT == data_format_; }
  bool use_bloom_filter() const { return use_bloom_filter_; }
  int32_t get_block_size() const { return block_size_; }
  std::vector<common::CompressionType> &get_compress_types() { return compress_types_; }
#ifndef NDEBUG
  void set_block_size(int32_t value) { block_size_ = value; }
#endif // NDEBUG

  static const int64_t ENGINE_ATTRIBUTE_VERSION = 1;
  DECLARE_SERIALIZATION()
  DECLARE_TO_STRING()

private:
  enum EngineAttributeType
  {
    INVALID_ENGINE_ATTRIBUTE_TYPE = 0,
    DATA_FORMAT_ATTRIBUTE = 1,
    COMPRESSION_ATTRIBUTE = 2,
    BLOOM_FILTER_ATTRIBUTE = 3,
    BLOCK_SIZE_ATTRIBUTE = 4,
  }; 

  enum DataFormat
  {
    INVALID_DATA_FORMAT = 0,
    ROW_FORMAT = 1,
    COLUMNAR_FORMAT = 2,
  };

  static const int32_t DEFAULT_BLOCK_SIZE = 16 * 1024; // 16KB
  static const int64_t BLOCK_SIZE_SHIFT_MAX = 20; // 1MB
  static const char *ENGINE_ATTRIBUTE_NAMES[];

  bool compare_str(const char *cstr, const std::string &str);
  bool is_data_format_attribute(const std::string &name);
  bool is_block_size_attribute(const std::string &name);
  bool is_bloom_filter_attribute(const std::string &name);
  bool is_compression_attribute(const std::string &name);
  bool check_data_format_validate(const DataFormat data_format) const;
  bool check_block_size_validate(const int32_t value) const;
  EngineAttributeType get_attribute_type(const std::string &name);
  int parse_data_format_attribute(const std::string &value);
  int parse_block_size_attribute(const int32_t value);
  int parse_compression_attribute(const std::string &value);

private:
  DataFormat data_format_;
  int32_t block_size_;
  bool use_bloom_filter_;
  std::vector<common::CompressionType> compress_types_;
};

} // namespace schema
} // namespace smartengine