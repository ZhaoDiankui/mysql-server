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

namespace smartengine
{
namespace util
{

class DIOHelper
{
public:
  static const int64_t DIO_ALIGN_SIZE; 
  
  static bool is_aligned(const int64_t val) { return (0 == (val % DIO_ALIGN_SIZE)); }

  static int64_t align_offset(const int64_t offset) { return (offset - (offset & (DIO_ALIGN_SIZE - 1))); }

  static int64_t align_size(const int64_t size) { return ((size + DIO_ALIGN_SIZE - 1) & (~(DIO_ALIGN_SIZE - 1))); }
};

} // namespace util
} // namespace smartengine