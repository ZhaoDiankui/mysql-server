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

#pragma  once

#include <cstdint>
#include "options/options.h"

namespace smartengine
{
namespace util
{

class Compressor
{
public:
  Compressor() : type_(common::kNoCompression) {}
  virtual ~Compressor() {}

  virtual int compress(const char *raw_data,
                       const int64_t raw_data_size,
                       char *compress_buf,
                       const int64_t compress_buf_size,
                       int64_t &compressed_data_size);

  virtual int uncompress(const char *compresed_data,
                         const int64_t compressed_data_size,
                         char *raw_buf,
                         const int64_t raw_buf_size,
                         int64_t &raw_data_size);

  virtual int get_max_compress_size(const int64_t raw_data_size,
                                    int64_t &max_compress_size) const;

  virtual common::CompressionType get_compress_type() const { return type_; }

protected:
  virtual int inner_compress(const char *raw_data,
                             const int64_t raw_data_size,
                             char *compress_buf,
                             const int64_t compress_buf_size,
                             int64_t &compressed_data_size) = 0;

  virtual int inner_uncompress(const char *compressed_data,
                               const int64_t compressed_data_size,
                               char *raw_buf,
                               const int64_t raw_buf_size,
                               int64_t &raw_data_size) = 0;

  virtual int64_t inner_get_max_compress_size(const int64_t raw_data_size) const = 0;

protected:
  common::CompressionType type_;
};

} //namespace util
} //namespace smartengine