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

#include "change_info.h"
#include "util/status.h"

namespace smartengine
{
using namespace common;
namespace storage
{
ExtentChange::ExtentChange() : layer_position_(), extent_id_(), flag_(F_INIT) {}

ExtentChange::ExtentChange(const LayerPosition &layer_position, const ExtentId &extent_id, uint8_t flag)
    : layer_position_(layer_position),
      extent_id_(extent_id),
      flag_(flag)
{}

ExtentChange::ExtentChange(const ExtentChange &extent_change)
    : layer_position_(extent_change.layer_position_),
      extent_id_(extent_change.extent_id_),
      flag_(extent_change.flag_)
{}

ExtentChange::~ExtentChange() {}

ExtentChange &ExtentChange::operator=(const ExtentChange &extent_change)
{
  layer_position_ = extent_change.layer_position_;
  extent_id_ = extent_change.extent_id_;
  flag_ = extent_change.flag_;

  return *this;
}


DEFINE_COMPACTIPLE_SERIALIZATION(ExtentChange, layer_position_, extent_id_, flag_)

ChangeInfo::ChangeInfo() :
    extent_change_info_(),
    lob_extent_change_info_(),
    task_type_(db::TaskType::INVALID_TYPE_TASK)
{
}

void ChangeInfo::clear()
{
  extent_change_info_.clear();
  lob_extent_change_info_.clear();
}

int ChangeInfo::add_extent(const LayerPosition &layer_position, const ExtentId &extent_id)
{
  ExtentChange extent_change(layer_position, extent_id, ExtentChange::F_ADD);
  return push_extent_change(layer_position.level_, extent_change);
}

int ChangeInfo::delete_extent(const LayerPosition &layer_position, const ExtentId &extent_id)
{
  ExtentChange extent_change(layer_position, extent_id, ExtentChange::F_DEL);
  return push_extent_change(layer_position.level_, extent_change);
}

int ChangeInfo::replace_extent(const LayerPosition &layer_position, const ExtentId &old_extent, const ExtentId &new_extent)
{
  int ret = Status::kOk;

  if (FAILED(delete_extent(layer_position, old_extent))) {
    SE_LOG(WARN, "fail to delete old extent", K(ret), K(layer_position), K(old_extent));
  } else if (FAILED(add_extent(layer_position, new_extent))) {
    SE_LOG(WARN, "fail to add new extent", K(ret), K(layer_position), K(new_extent));
  }

  return ret;
}

int ChangeInfo::add_large_object_extent(const ExtentId &extent_id)
{
  LayerPosition dummy_layer_position(0, 0);
  ExtentChange extent_change(dummy_layer_position, extent_id, ExtentChange::F_ADD);
  return push_large_object_extent_change(extent_change);
}

int ChangeInfo::delete_large_object_extent(const ExtentId &extent_id)
{
  LayerPosition dummy_layer_position(0, 0);
  ExtentChange extent_change(dummy_layer_position, extent_id, ExtentChange::F_DEL);
  return push_large_object_extent_change(extent_change);
}

int ChangeInfo::merge(const ChangeInfo &change_info)
{
  int ret = Status::kOk;
  const std::unordered_map<int64_t, std::vector<ExtentChange>> &extent_change_info = change_info.extent_change_info_;

  for (auto right_iter = extent_change_info.begin(); SUCCED(ret) && right_iter != extent_change_info.end(); ++right_iter) {
    auto left_iter = extent_change_info_.find(right_iter->first);
    if (extent_change_info_.end() == left_iter) {
      if (!(extent_change_info_.emplace(right_iter->first, right_iter->second).second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "fail to emplace to extent_change_info_", K(ret), "level", right_iter->first);
      }
    } else {
      left_iter->second.insert(left_iter->second.begin(), right_iter->second.begin(), right_iter->second.end());
    }
  }

  for (uint32_t i = 0; i < change_info.lob_extent_change_info_.size(); ++i) {
    lob_extent_change_info_.push_back(change_info.lob_extent_change_info_.at(i));
  }

  return ret;
}

int ChangeInfo::push_extent_change(const int64_t level, const ExtentChange &extent_change)
{
  int ret = Status::kOk;
  auto iter = extent_change_info_.find(level);
  if (extent_change_info_.end() != iter) {
    iter->second.push_back(extent_change);
  } else {
    std::vector<ExtentChange> extent_change_vec;
    extent_change_vec.push_back(extent_change);
    if (!(extent_change_info_.emplace(level, extent_change_vec).second)) {
      ret = Status::kErrorUnexpected;
    }
  }
  return ret;
}

int ChangeInfo::push_large_object_extent_change(const ExtentChange &extent_change)
{
  lob_extent_change_info_.push_back(extent_change);
  return Status::kOk;
}

DEFINE_COMPACTIPLE_SERIALIZATION(ChangeInfo, extent_change_info_, lob_extent_change_info_, task_type_)
} //namespace storage
} //namespace smartengine
