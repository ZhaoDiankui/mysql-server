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

#include "storage/large_object_extent_manager.h"
#include "storage/change_info.h"
#include "storage/extent_meta_manager.h"
#include "storage/extent_space_manager.h"
#include "storage/storage_meta_struct.h"
#include "logger/log_module.h"

namespace smartengine
{
using namespace common;
namespace storage
{
LargeObjectExtentMananger::LargeObjectExtentMananger()
    : is_inited_(false),
      lob_extent_(),
      delete_lob_extent_(),
      lob_extent_wait_to_recycle_()
{
}

LargeObjectExtentMananger::~LargeObjectExtentMananger()
{
  destroy();
}

void LargeObjectExtentMananger::destroy()
{
  if (is_inited_)  {
    lob_extent_.clear();
    delete_lob_extent_.clear();
    lob_extent_wait_to_recycle_.clear();
    is_inited_ = false;
  }
}

//TODO(Zhao Dongsheng): the init function do nothing now.
int LargeObjectExtentMananger::init()
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "init twice", K(ret));
  } else {
    is_inited_ = true;
  }

  return ret;
}

int LargeObjectExtentMananger::apply(const ChangeInfo &change_info, common::SequenceNumber sequence_number)
{
  int ret = Status::kOk;
  ExtentMeta *extent_meta = nullptr;
  const std::vector<ExtentChange> extent_changes = change_info.lob_extent_change_info_;
  delete_lob_extent_.clear();
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < extent_changes.size(); ++i) {
      ExtentChange extent_change = extent_changes.at(i);
      if (IS_NULL(extent_meta = ExtentMetaManager::get_instance().get_meta(extent_change.extent_id_))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "fail to get extent_meta", K(ret), K(extent_change));
      } else {
        if (extent_change.is_add()) {
          if (FAILED(add_extent(extent_meta))) {
            SE_LOG(WARN, "fail to add extent", K(ret), K(*extent_meta));
          }
        } else if (extent_change.is_delete()) {
          if (FAILED(delete_extent(extent_meta))) {
            SE_LOG(WARN, "fail to delete extent", K(ret), K(*extent_meta));
          }
        } else {
          ret = Status::kNotSupported;
          SE_LOG(WARN, "unsupported extent change type", K(ret), K(extent_change));
        }
      }
    }

    if (SUCCED(ret)) {
      if (FAILED(update(sequence_number))) {
        SE_LOG(WARN, "fail to update", K(ret), K(sequence_number));
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::recycle(common::SequenceNumber sequence_number, bool for_recovery)
{
  int ret = Status::kOk;
  common::SequenceNumber lob_extent_seq = 0;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else {
    for (auto iter = lob_extent_wait_to_recycle_.begin(); SUCCED(ret) && lob_extent_wait_to_recycle_.end() != iter;) {
      lob_extent_seq = iter->first;
      if (lob_extent_seq < sequence_number) {
        if (FAILED(recycle_extents(iter->second, for_recovery))) {
          SE_LOG(WARN, "fail to recycle extents", K(ret), K(sequence_number), K(lob_extent_seq), "extents_size", iter->second.size());
        } else {
          //prevent iterator failture, use rt as new iter
          iter = lob_extent_wait_to_recycle_.erase(iter);
          SE_LOG(INFO, "success to recycle some lob extents", K(lob_extent_seq));
        }
      } else {
        ++iter;
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::force_recycle(bool for_recovery)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else {
    for (auto iter = lob_extent_.begin(); lob_extent_.end() != iter; ++iter) {
      if (FAILED(recycle_extent(iter->second, for_recovery))) {
        SE_LOG(WARN, "fail to recycle extent", K(ret), "extent_id", iter->first);
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::recover_extent_space()
{
  int ret = Status::kOk;
  ExtentMeta *extent_meta = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else {
    for (auto iter = lob_extent_.begin(); SUCCED(ret) && lob_extent_.end() != iter; ++iter) {
      ExtentId extent_id(iter->first);
      if (IS_NULL(extent_meta = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, ExtentMeta must not nullptr", K(ret), K(extent_id));
      } else if (FAILED(ExtentSpaceManager::get_instance().reference(extent_meta->table_space_id_,
                                                                     extent_meta->extent_space_type_,
                                                                     extent_meta->prefix_,
                                                                     extent_id))) {
        SE_LOG(WARN, "fail to to reference lob extent", K(ret), "prefix", extent_meta->prefix_, K(extent_id));
      } else {
        SE_LOG(INFO, "success to refrence lob extent", K(*extent_meta));
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::serialize(char *buf, int64_t buf_length, int64_t &pos) const
{
  int ret = Status::kOk;
  int64_t size = get_serialize_size();
  int64_t version = LARGE_OBJECT_EXTENT_MANAGER_VERSION;
  std::vector<ExtentId> extent_ids;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else if (IS_NULL(buf) || buf_length < 0 || pos >= buf_length) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(buf), K(buf_length), K(pos));
  } else if (FAILED(get_all_extent_ids(extent_ids))) {
    SE_LOG(WARN, "fail to get all extent ids", K(ret));
  } else {
    *((int64_t *)(buf + pos)) = size;
    pos += sizeof(size);
    *((int64_t *)(buf + pos)) = version;
    pos += sizeof(version);
    if (FAILED(util::serialize_v(buf, buf_length, pos, extent_ids))) {
      SE_LOG(WARN, "fail to serialize extent ids", K(ret));
    }
  }

  return ret;
}

int LargeObjectExtentMananger::deserialize(const char *buf, int64_t buf_length, int64_t &pos)
{
  int ret = Status::kOk;
  int64_t size = 0;
  int64_t version = 0;
  std::vector<ExtentId> extent_ids;

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else if (IS_NULL(buf) || buf_length < 0 || pos >= buf_length) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(buf), K(buf_length), K(pos));
  } else {
    size = *((int64_t *)(buf + pos));
    pos += sizeof(size);
    version = *((int64_t *)(buf + pos));
    pos += sizeof(version);
    if (FAILED(util::deserialize_v(buf, buf_length, pos, extent_ids))) {
      SE_LOG(WARN, "fail to deserialize extent ids", K(ret));
    } else if (FAILED(build_lob_extent(extent_ids))) {
      SE_LOG(WARN, "fail to build lob extent", K(ret));
    }
  }

  return ret;
}

int64_t LargeObjectExtentMananger::get_serialize_size() const
{
  int ret = Status::kOk;
  int64_t size = 0;
  std::vector<ExtentId> extent_ids;

  if (FAILED(get_all_extent_ids(extent_ids))) {
    SE_LOG(WARN, "fail to get all extent ids", K(ret));
  } else {
    size += 2 * sizeof(int64_t); //size and version
    size += util::get_serialize_v_size(extent_ids);
  }
  return size;
}
int LargeObjectExtentMananger::add_extent(ExtentMeta *extent_meta)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else if (IS_NULL(extent_meta)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(extent_meta));
  } else if (!(lob_extent_.emplace(extent_meta->extent_id_.id(), extent_meta).second)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to emplace large object extent", K(ret), K(*extent_meta));
  } else {
    extent_meta->ref();
    SE_LOG(INFO, "success to add lob extent", K(*extent_meta));
  }

  return ret;
}

int LargeObjectExtentMananger::delete_extent(ExtentMeta *extent_meta)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else if (IS_NULL(extent_meta)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(extent_meta));
  } else {
    auto iter = lob_extent_.find(extent_meta->extent_id_.id());
    if (lob_extent_.end() == iter) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, the extent to drop not exist", K(ret), K(*extent_meta));
    } else {
      delete_lob_extent_.push_back(extent_meta);
      SE_LOG(INFO, "success to delete lob extent", K(*extent_meta));
    }
  }

  return ret;
}

int LargeObjectExtentMananger::update(common::SequenceNumber sequence_number)
{
  int ret = Status::kOk;
  ExtentMeta *extent_meta = nullptr;
  uint32_t erase_count = 0;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else {
    if (delete_lob_extent_.size() > 0) {
      for (uint32_t i = 0; SUCCED(ret) && i < delete_lob_extent_.size(); ++i) {
        if (IS_NULL(extent_meta = delete_lob_extent_.at(i))) {
          ret = Status::kErrorUnexpected;
          SE_LOG(WARN, "unexpected error, extent meta must not nullptr", K(ret), K(i), KP(extent_meta));
        } else if (1 != (erase_count = lob_extent_.erase(extent_meta->extent_id_.id()))) {
          ret = Status::kErrorUnexpected;
          SE_LOG(WARN, "unexpected error, erased count not expected", K(ret), K(erase_count), K(*extent_meta));
        }
      }

      if (SUCCED(ret)) {
        if (!(lob_extent_wait_to_recycle_.emplace(sequence_number, delete_lob_extent_).second)) {
          ret = Status::kErrorUnexpected;
          SE_LOG(WARN, "unexpected error, fail to emplace to recycle extent", K(ret), K(sequence_number));
        }
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::recycle_extents(const std::vector<ExtentMeta *> &extents, bool for_recovery)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else if (UNLIKELY(extents.size() <= 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), "extents_size", extents.size());
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < extents.size(); ++i) {
      if (FAILED(recycle_extent(extents.at(i), for_recovery))) {
        SE_LOG(WARN, "fail to recycle extent", K(ret), K(i));
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::recycle_extent(ExtentMeta *extent_meta, bool for_recovery)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else if (IS_NULL(extent_meta)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(extent_meta));
  } else {
    SE_LOG(INFO, "try to recycle extent", K(*extent_meta));
    if (extent_meta->unref()) {
      ExtentId extent_id = extent_meta->extent_id_;
      if (!for_recovery) {
        if (FAILED(ExtentSpaceManager::get_instance().recycle(extent_meta->table_space_id_,
                                                              extent_meta->extent_space_type_,
                                                              extent_meta->prefix_,
                                                              extent_id))) {
          SE_LOG(WARN, "fail to recycle extent", K(ret), K(extent_id));
        } else {
          SE_LOG(INFO, "success to recycle lob extent", K(extent_id));
        }
      } else {
        if (FAILED(ExtentMetaManager::get_instance().recycle_meta(extent_id))) {
          SE_LOG(WARN, "fail to recycle lob extent", K(ret), K(extent_id));
        } else {
          SE_LOG(INFO, "success to recycle lob extent", K(extent_id));
        }
      }
    } else {
      ret = Status::kErrorUnexpected;
      SE_LOG(ERROR, "if try to recycle lob extent, expect it must been recycled", K(ret), K(*extent_meta));
    }
  }

  return ret;
}

int LargeObjectExtentMananger::get_all_extent_ids(std::vector<ExtentId> &extent_ids) const
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should been inited first", K(ret));
  } else {
    for (auto iter = lob_extent_.begin(); SUCCED(ret) && lob_extent_.end() != iter; ++iter) {
      extent_ids.push_back(iter->first);
    }
  }

  return ret;
}

int LargeObjectExtentMananger::build_lob_extent(std::vector<ExtentId> &extent_ids)
{
  int ret = Status::kOk;
  ExtentMeta *extent_meta = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "LargeObjectExtentMananger should beeen inited first", K(ret));
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < extent_ids.size(); ++i) {
      ExtentId extent_id = extent_ids.at(i);
      if (IS_NULL(extent_meta = ExtentMetaManager::get_instance().get_meta(extent_id))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "fail to get meta", K(ret), K(i), K(extent_id));
      } else if (!(lob_extent_.emplace(extent_id.id(), extent_meta).second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "fail to emplace to lob extent", K(ret), K(i), K(extent_id), K(*extent_meta));
      } else {
        extent_meta->ref();
        SE_LOG(INFO, "success to add lob extent", K(extent_id));
      }
    }
  }

  return ret;
}

int LargeObjectExtentMananger::deserialize_and_dump(const char *buf, int64_t buf_len, int64_t &pos,
                                                    char *str_buf, int64_t str_buf_len, int64_t &str_pos)
{
  int ret = Status::kOk;
  int64_t size = 0;
  int64_t version = 0;
  std::vector<ExtentId> extent_ids;

  if (IS_NULL(buf) || buf_len < 0 || pos >= buf_len) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(buf), K(buf_len), K(pos));
  } else {
    size = *((int64_t *)(buf + pos));
    pos += sizeof(size);
    version = *((int64_t *)(buf + pos));
    pos += sizeof(version);
    if (FAILED(util::deserialize_v(buf, buf_len, pos, extent_ids))) {
      SE_LOG(WARN, "fail to deserialize extent ids", K(ret));
    } else {
      util::databuff_printf(str_buf, str_buf_len, str_pos, "{");
      util::databuff_print_json_wrapped_kv(str_buf, str_buf_len, str_pos, "lob", "");
      for (uint32_t i = 0; i < extent_ids.size(); ++i) {
        util::databuff_print_json_wrapped_kv(str_buf, str_buf_len, str_pos, "extent_id", extent_ids.at(i));
      }
      util::databuff_printf(str_buf, str_buf_len, str_pos, "}");
    }
  }

  return ret;
}
} //namespace storage
} //namespace smartengine
