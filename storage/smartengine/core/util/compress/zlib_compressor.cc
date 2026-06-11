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

#ifdef HAVE_ZLIB

#include "util/compress/zlib_compressor.h"
#include "logger/log_module.h"
#include "util/status.h"

#include "zlib.h"

namespace smartengine
{
using namespace common;
namespace util
{

int ZLIBCompressor::inner_compress(const char *raw_data,
                                   const int64_t raw_data_size,
                                   char *compress_buf,
                                   const int64_t compress_buf_size,
                                   int64_t &compressed_data_size)
{
  int ret = Status::kOk;
  int zlib_ret = Z_OK;
  int64_t compressed_size = compress_buf_size;

  if (Z_OK != (zlib_ret = compress2(reinterpret_cast<Bytef*>(compress_buf),
                                    reinterpret_cast<uLongf*>(&compressed_size),
                                    reinterpret_cast<const Bytef*>(raw_data),
                                    static_cast<uLong>(raw_data_size),
                                    7 /**compress_level*/))) {
    ret = Status::kNotCompress;
#ifndef NDEBUG
    SE_LOG(WARN, "Failed to compress data by Zlib", K(ret), K(zlib_ret));
#endif
  } else {
    compressed_data_size = compressed_size;
  }

  return ret;
}

int ZLIBCompressor::inner_uncompress(const char *compressed_data,
                                     const int64_t compressed_data_size,
                                     char *raw_buf,
                                     const int64_t raw_buf_size,
                                     int64_t &raw_data_size)
{
  int ret = Status::kOk;
  int zlib_ret = Z_OK;
  int64_t uncompressed_size = raw_buf_size;

  if (Z_OK != (zlib_ret = ::uncompress(reinterpret_cast<Bytef*>(raw_buf),
                                       reinterpret_cast<uLongf*>(&uncompressed_size),
                                       reinterpret_cast<const Bytef*>(compressed_data),
                                       static_cast<uLong>(compressed_data_size)))) {
    ret = Status::kCorruption;
    SE_LOG(WARN, "Failed to decompress data by Zlib", K(ret), K(zlib_ret));
  } else {
    raw_data_size = uncompressed_size;
  }

  return ret;
}

int64_t ZLIBCompressor::inner_get_max_compress_size(const int64_t raw_data_size) const
{
  return compressBound(raw_data_size);
}

} // namespace util
} // namespace smartengine

#endif // HAVE_ZLIB