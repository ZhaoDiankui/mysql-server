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
#include "storage/storage_common.h"
#include "util/autovector.h"
#include "util/compress/compressor_helper.h"
#include "util/serialization.h"

namespace smartengine
{
namespace table
{
struct LargeValue
{
public:
  static const int64_t LARGE_VALUE_VERSION = 1;

  int64_t raw_size_;
  int64_t size_;
  int8_t compress_type_;
  util::autovector<storage::ExtentId> extents_;

  LargeValue();
  ~LargeValue();

  void destroy();
  void reuse();
  int convert_to_normal_format(const common::Slice &large_object_value, common::Slice &normal_value);

  DECLARE_COMPACTIPLE_SERIALIZATION(LARGE_VALUE_VERSION)
private:
  int prepare_buf(int64_t size, int64_t raw_size, common::CompressionType compress_type);

private:
  // The follow variables are not persistent.
  util::CompressorHelper compressor_helper_;
  char *data_buf_;
  int64_t data_buf_size_;
  char *raw_data_buf_;
  int64_t raw_data_buf_size_;
};

struct LargeObject
{
  static const int64_t LARGE_OBJECT_VERSION = 1;
  static const int64_t LARGE_OBJECT_THRESHOLD_SIZE = 1536 * 1024;
  static const int64_t MAX_CONVERTED_LARGE_OBJECT_SIZE = 36 * 1024;

  std::string key_;
  LargeValue value_;

  LargeObject();
  ~LargeObject();

  DECLARE_COMPACTIPLE_SERIALIZATION(LARGE_OBJECT_VERSION)
};

} // namespace table
} // namespace smartengine