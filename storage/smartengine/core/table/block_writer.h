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
namespace common
{
class Slice;
} // namespace common

namespace table
{
struct BlockInfo;

class BlockWriter
{
public:
  BlockWriter() {}
  virtual ~BlockWriter() {}

  virtual void reuse() = 0;
  virtual int append(const common::Slice &key, const common::Slice &value) = 0;
  virtual int build(common::Slice &block, BlockInfo &block_info) = 0;
  virtual int64_t current_size() const = 0;
  virtual int64_t future_size(const uint32_t key_size, const uint32_t value_size) const = 0;
  virtual bool is_empty() const = 0;
};

} // namespace table
} // namespace smartengine