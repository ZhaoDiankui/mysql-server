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
#include "util/compress/lz4_compressor.h"
#include "util/compress/zlib_compressor.h"
#include "util/compress/zstd_compressor.h"

namespace smartengine
{
namespace util
{
class Compressor;

class CompressorFactory
{
public:
  static CompressorFactory &get_instance();

  int alloc_compressor(common::CompressionType compress_type, Compressor *&compressor);
  int free_compressor(Compressor *&compressor);

private:
  CompressorFactory() {}
  virtual ~CompressorFactory() {}
};

} // namespace util
} // namespace smartengine