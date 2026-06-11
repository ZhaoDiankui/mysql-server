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

#include "options/options.h"
#include "util/data_buffer.h"
#include <cstdint>

namespace smartengine
{
namespace util
{
class Compressor;

// CompressHelper is not thread-safe.
class CompressorHelper
{
public:
  CompressorHelper();
  ~CompressorHelper();

  void reset();

  int compress(const common::Slice &raw_data,
               common::CompressionType expected_compress_type,
               common::Slice &compressed_data,
               common::CompressionType &actual_compress_type);

  int uncompress(const common::Slice &compressed_data,
                 common::CompressionType compress_type,
                 char *raw_buf,
                 int64_t raw_data_size,
                 common::Slice &raw_data);
private:
  int setup_compressor(common::CompressionType compress_type);
  int actual_compress(const common::Slice &raw_data,
                      common::CompressionType expected_compress_type,
                      common::Slice &compressed_data,
                      common::CompressionType &actual_compress_type);

private:
  util::Compressor *compressor_;
  util::AutoBufferWriter compress_buf_;
};

} // namespace util
} // namespace smartengine