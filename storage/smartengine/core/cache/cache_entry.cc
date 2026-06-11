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

#include "cache/cache_entry.h"
#include "logger/log_module.h"
#include "storage/io_extent.h"

namespace smartengine
{
using namespace common;

namespace cache
{
CacheEntryKey::CacheEntryKey() : is_inited_(false), key_prefix_{0}, key_prefix_size_(0) {}
CacheEntryKey::~CacheEntryKey() {}

int CacheEntryKey::setup(const storage::IOExtent *extent)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "CacheEntryKey has been inited", K(ret));
  } else if (IS_NULL(extent)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(extent));
  } else if (0 >= (key_prefix_size_ = extent->get_unique_id(key_prefix_, MAX_CACHE_KEY_PREFIX_SIZE))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "fail to generate cache key prefix", K(ret), K_(key_prefix_size));
  } else {
    is_inited_ = true;
  }

  return ret;
}

int CacheEntryKey::generate(const int64_t id, char *buf, Slice &key)
{
  int ret = Status::kOk;
  char *end = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "CachableEntry should be inited", K(ret));
  } else {
    memcpy(buf, key_prefix_, key_prefix_size_);
    end = util::EncodeVarint64(buf + key_prefix_size_, id);
    
    key.assign(buf, end - buf);
  }

  return ret;
}

} // namespace cache
} // namespace smartengine
