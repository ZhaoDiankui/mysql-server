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

#include "storage/table_space.h"

#include "storage/extent_space_file.h"
#include "storage/extent_space_obj.h"

namespace smartengine
{
using namespace common;
namespace storage
{
TableSpace::TableSpace(util::Env *env, const util::EnvOptions &env_options)
    : is_inited_(false),
      env_(env),
      env_options_(env_options),
      table_space_id_(0),
      table_space_mutex_(),
      db_paths_(),
      extent_space_map_(),
      index_id_set_()
{
}

TableSpace::~TableSpace()
{
  destroy();
}

void TableSpace::destroy()
{
  if (is_inited_) {
    for (auto iter = extent_space_map_.begin(); extent_space_map_.end() != iter; ++iter) {
      MOD_DELETE_OBJECT(ExtentSpace, iter->second);
    }
    extent_space_map_.clear();
    index_id_set_.clear();
    is_inited_ = false;
  }
}

int TableSpace::create(const CreateTableSpaceArgs &args)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "TableSpace has been inited", K(ret));
  } else if (UNLIKELY(!args.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret));
  } else {
    // ugly code, old code use index to represent space type, but it can not
    // express object store attribute, so when enable object store, we get
    // objstore from global env.
    assert(args.db_paths_.size() == 1);
    int32_t extent_space_type = FILE_EXTENT_SPACE;
    CreateExtentSpaceArgs extent_space_args(
        args.table_space_id_, extent_space_type, args.db_paths_[0]);
    ::objstore::ObjectStore *objstore = nullptr;
    Status objstore_status = env_->GetObjectStore(objstore);
    if (!objstore_status.ok()) {
      // extent_space_args is aready set.
      extent_space = MOD_NEW_OBJECT(memory::ModId::kExtentSpaceMgr,
                                    FileExtentSpace, env_, env_options_);
    } else {
      // extent_space_type & extent_space_args need to revise.
      extent_space_type = OBJECT_EXTENT_SPACE;
      extent_space_args.extent_space_type_ = extent_space_type;
      // args.db_paths_ is the local path for manifest file and so on.
      extent_space_args.db_path_.path = env_->GetObjectStoreBucket();
      extent_space_args.db_path_.target_size = UINT64_MAX;
      extent_space =
          MOD_NEW_OBJECT(memory::ModId::kExtentSpaceMgr, ObjectExtentSpace,
                         env_, env_options_, objstore);
    }
    if (IS_NULL(extent_space)) {
      ret = Status::kMemoryLimit;
      SE_LOG(WARN, "fail to allocate memory for ExtentSpace", K(ret));
    } else if (FAILED(extent_space->create(extent_space_args))) {
      SE_LOG(ERROR, "fail to create extent space", K(ret), K(extent_space_type),
             K(extent_space_args));
    } else if (!(extent_space_map_.emplace(extent_space_type, extent_space)
                     .second)) {
      ret = Status::kErrorUnexpected;
      SE_LOG(ERROR, "unexpected error, fail to emplace extent_space", K(ret),
             K(extent_space_type));
    }

    if (SUCCED(ret)) {
      table_space_id_ = args.table_space_id_;
      db_paths_ = args.db_paths_;
      is_inited_ = true;
    }
  }

  return ret;
}

int TableSpace::remove()
{
  int ret = Status::kOk;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (!is_free_unsafe()) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, TableSpace is not free, can't been remove", K(ret));
  } else {
    for (auto iter = extent_space_map_.begin(); SUCCED(ret) && extent_space_map_.end() != iter; ++iter) {
      if (FAILED(iter->second->remove())) {
        SE_LOG(WARN, "fail to remove ExtentSpace", K(ret), "extent_space_type", iter->first);
      }
    }
  }

  return ret;
}

int TableSpace::register_subtable(const int64_t index_id)
{
  int ret = Status::kOk;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace shoule been inited first", K(ret));
  } else if (!(index_id_set_.emplace(index_id).second)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "fail to emplace subtable", K(ret), K(index_id));
  } else {
    SE_LOG(INFO, "success to register subtable", K(index_id));
  }

  return ret;
}

int TableSpace::unregister_subtable(const int64_t index_id)
{
  int ret = Status::kOk;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (1 != (index_id_set_.erase(index_id))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to erase from subtable map", K(ret), K(index_id));
  } else {
    SE_LOG(INFO, "success to unregister subtable", K(index_id));
  }

  return ret;
}

int TableSpace::allocate(const int32_t extent_space_type,
                         const std::string prefix,
                         ExtentIOInfo &io_info)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (IS_NULL(extent_space = get_extent_space(extent_space_type))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to get extent space", K(ret), K(extent_space_type));
  } else if (FAILED(extent_space->allocate(prefix, io_info))) {
    SE_LOG(WARN, "fail to allocate extent from ExtentSpace", K(ret), K(extent_space_type));
  } else {
    SE_LOG(DEBUG, "success to allocate extent", K(io_info));
  }

  return ret;
}

int TableSpace::recycle(const int32_t extent_space_type,
                        const std::string prefix,
                        const ExtentId extent_id)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited firtst", K(ret));
  } else if (IS_NULL(extent_space = get_extent_space(extent_space_type))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to get extent space", K(ret), K(extent_space_type));
  } else if (FAILED(extent_space->recycle(prefix, extent_id))) {
    SE_LOG(WARN, "fail to recycle extent", K(ret), K(extent_id));
  } else {
    SE_LOG(DEBUG, "success to recycle extent", K(extent_id));
  }

  return ret;
}

int TableSpace::reference_if_need(const int32_t extent_space_type,
                                  const std::string prefix,
                                  const ExtentId extent_id,
                                  ExtentIOInfo &io_info,
                                  bool &existed)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited firtst", K(ret));
  } else if (IS_NULL(extent_space = get_extent_space(extent_space_type))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to get extent space", K(ret), K(extent_space_type));
  } else if (FAILED(extent_space->reference_if_need(prefix, extent_id, io_info, existed))) {
    SE_LOG(WARN, "fail to reference extent", K(ret), K(extent_id));
  } else {
    SE_LOG(DEBUG, "success to reference extent", K(extent_id));
  }

  return ret;
}

int TableSpace::move_extens_to_front(const ShrinkInfo &shrink_info,
                                     std::unordered_map<int64_t, ExtentIOInfo> &replace_map)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (UNLIKELY(!shrink_info.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(shrink_info));
  } else if (index_id_set_ != shrink_info.index_id_set_) {
    SE_LOG(INFO, "there has ddl happened during shrink, cancle this shrink");
  } else if (IS_NULL(extent_space = get_extent_space(shrink_info.extent_space_type_))){
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, ExtentSpace must not nullptr", K(ret), K(shrink_info));
  //TODO:yuanfeng check the extent count
  } else if (FAILED(extent_space->move_extens_to_front(shrink_info.shrink_extent_count_, replace_map))) {
    SE_LOG(WARN, "fail to move extent to front", K(ret), K(shrink_info));
  } else {
    SE_LOG(INFO, "success to move extent to front", K(shrink_info));
  }

  return ret;
}

int TableSpace::shrink(const ShrinkInfo &shrink_info)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (UNLIKELY(!shrink_info.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret));
  } else if (IS_NULL(extent_space = get_extent_space(shrink_info.extent_space_type_))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, ExtentSpace must not nullptr", K(ret), K(shrink_info));
  } else if (FAILED(extent_space->shrink(shrink_info.shrink_extent_count_))) {
    SE_LOG(WARN, "fail to shrink extent space", K(ret), K(shrink_info));
  }

  return ret;
}

int TableSpace::get_shrink_info_if_need(const ShrinkCondition &shrink_condition, std::vector<ShrinkInfo> &shrink_infos)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;
  bool need_shrink = false;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (is_dropped_unsafe()) {
    SE_LOG(INFO, "the table space is dropped", K_(table_space_id));
  } else {
    for (auto iter = extent_space_map_.begin(); SUCCED(ret) && extent_space_map_.end() != iter; ++iter) {
      ShrinkInfo shrink_info;
      need_shrink = false;
      if (IS_NULL(extent_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, ExentSpace must not nullptr", K(ret), "extent_space_type", iter->first);
      } else if (FAILED(extent_space->get_shrink_info_if_need(shrink_condition, need_shrink, shrink_info))) {
        SE_LOG(WARN, "fail to get shrink info", K(ret), "extent_space_type", iter->first);
      } else {
        if (need_shrink) {
          shrink_info.shrink_condition_ = shrink_condition;
          shrink_info.index_id_set_ = index_id_set_;
          shrink_infos.push_back(shrink_info);
        }
      }
    }
  }

  return ret;
}

int TableSpace::get_shrink_info(const int32_t extent_space_type,
                                const ShrinkCondition &shrink_condition,
                                ShrinkInfo &shrink_info)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;
  bool need_shrink = false;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first",
        K(ret), K(extent_space_type), K(shrink_condition));
  } else if (is_dropped_unsafe()) {
    SE_LOG(INFO, "the table space is dropped", K_(table_space_id));
  } else {
    auto iter = extent_space_map_.find(extent_space_type);
    if (extent_space_map_.end() == iter) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, can't find the extent space",
          K(ret), K_(table_space_id), K(extent_space_type));
    } else if (IS_NULL(extent_space = iter->second)) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, extent space must not nullptr",
          K(ret), K_(table_space_id), K(extent_space_type));
    } else if (FAILED(extent_space->get_shrink_info_if_need(shrink_condition,
                                                            need_shrink,
                                                            shrink_info))) {
      SE_LOG(WARN, "fail to get shrink info", K(ret), K(extent_space_type));
    } else {
      shrink_info.shrink_condition_ = shrink_condition;
      shrink_info.index_id_set_ = index_id_set_;
      SE_LOG(DEBUG, "success to get extent space shrink info", K(shrink_condition), K(shrink_info));
    }
  }

  return ret;
}

bool TableSpace::is_free()
{
  std::lock_guard<std::mutex> guard(table_space_mutex_);
  return is_free_unsafe();
}

bool TableSpace::is_dropped()
{
  std::lock_guard<std::mutex> guard(table_space_mutex_);
  return is_dropped_unsafe();
}

int TableSpace::get_data_file_stats(std::vector<DataFileStatistics> &data_file_stats)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else {
    for (auto iter = extent_space_map_.begin(); SUCCED(ret) && extent_space_map_.end() != iter; ++iter) {
      if (IS_NULL(extent_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, ExtentSpace must not nullptr", K(ret), "extent_space_type", iter->first);
      } else if (FAILED(extent_space->get_data_file_stats(data_file_stats))) {
        SE_LOG(WARN, "fail to get data file stats", K(ret));
      }
    }
  }

  return ret;
}

int TableSpace::add_data_file(DataFile *data_file)
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else if (IS_NULL(data_file)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(data_file));
  } else if (IS_NULL(extent_space = get_extent_space(data_file->get_extent_space_type()))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to get extent space", K(ret), "extent_space_type", data_file->get_extent_space_type());
  } else if (FAILED(extent_space->add_data_file(data_file))) {
    SE_LOG(WARN, "fail to add datafile to extentspace", K(ret));
  }

  return ret;
}

int TableSpace::rebuild()
{
  int ret = Status::kOk;
  ExtentSpace *extent_space = nullptr;

  std::lock_guard<std::mutex> guard(table_space_mutex_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "TableSpace should been inited first", K(ret));
  } else {
    for (auto iter = extent_space_map_.begin(); SUCCED(ret) && extent_space_map_.end() != iter; ++iter) {
      if (IS_NULL(extent_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, ExtentSpace must not nullptr", K(ret), "extent_space_type", iter->first);
      } else if (FAILED(extent_space->rebuild())) {
        SE_LOG(WARN, "fail to rebuild extentspace", K(ret), "extent_space_type", iter->first);
      } else {
        SE_LOG(DEBUG, "success to rebuild tablespace", K_(table_space_id));
      }
    }
  }

  return ret;
}

ExtentSpace *TableSpace::get_extent_space(const int32_t extent_space_type)
{
  ExtentSpace *extent_space = nullptr;
  auto iter = extent_space_map_.find(extent_space_type);
  if (extent_space_map_.end() != iter) {
    extent_space = iter->second;
  }
  return extent_space;
}

bool TableSpace::is_free_unsafe()
{
  bool free = true;
  for (auto iter = extent_space_map_.begin(); extent_space_map_.end() != iter; ++iter) {
    if (!(iter->second->is_free())) {
      free = false;
      break;
    }
  }
  return free;

}

bool TableSpace::is_dropped_unsafe()
{
  return index_id_set_.empty();
}

} //namespace storage
} //namespace smartengine
