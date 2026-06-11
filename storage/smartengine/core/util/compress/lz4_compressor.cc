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

#ifdef HAVE_LZ4

#include "util/compress/lz4_compressor.h"
#include "logger/log_module.h"
#include "util/status.h"

#include "lz4.h"

namespace smartengine {
using namespace common;

namespace util {

int LZ4Compressor::inner_compress(const char *raw_data,
                                  const int64_t raw_data_size,
                                  char *compress_buf,
                                  const int64_t compress_buf_size,
                                  int64_t &compressed_data_size)
{
  int ret = Status::kOk;
  int64_t compressed_size = 0;

  if (0 >= (compressed_size = LZ4_compress_default(raw_data,
                                                   compress_buf,
                                                   raw_data_size,
                                                   compress_buf_size))) {
    ret = Status::kNotCompress;
#ifndef NDEBUG
    SE_LOG(WARN, "Failed to compress data by LZ4", K(compressed_size));
#endif
  } else {
    compressed_data_size = compressed_size;
  }

  return ret;
}

int LZ4Compressor::inner_uncompress(const char *compressed_data,
                                    const int64_t compressed_data_size,
                                    char *raw_buf,
                                    const int64_t raw_buf_size,
                                    int64_t &raw_data_size)
{
  int ret = Status::kOk;
  int64_t uncompressed_size = 0;

  if (0 >= (uncompressed_size = LZ4_decompress_safe(compressed_data,
                                                    raw_buf,
                                                    compressed_data_size,
                                                    raw_buf_size))) {
    ret = Status::kCorruption;
    SE_LOG(WARN, "Failed to uncompress data by LZ4", K(ret), K(uncompressed_size));
  } else {
    raw_data_size = uncompressed_size;
  }

  return ret;
}

int64_t LZ4Compressor::inner_get_max_compress_size(const int64_t raw_data_size) const
{
  return LZ4_compressBound(raw_data_size); 
}

} // namespace util
} // namespace smartengine

#endif // HAVE_LZ4