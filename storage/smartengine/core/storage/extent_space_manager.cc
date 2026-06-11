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

#include "storage/extent_meta_manager.h"
#include "storage/extent_space_manager.h"
#include "storage/storage_meta_struct.h"
#include "util/file_name.h"
#include "util/increment_number_allocator.h"
#include "util/sync_point.h"


namespace smartengine
{
using namespace common;
using namespace util;
using namespace memory;
namespace storage
{
ExtentSpaceManager::ExtentSpaceManager()
    : is_inited_(false),
      env_(nullptr),
      env_options_(),
      db_options_(),
      table_space_lock_(),
      table_space_id_(0),
      table_space_map_(),
      wait_remove_table_space_map_(),
      io_info_map_lock_(),
      extent_io_info_map_(),
      garbage_files_()
{}

ExtentSpaceManager::~ExtentSpaceManager()
{
  destroy();
}

void ExtentSpaceManager::destroy()
{
  if (is_inited_) {
    for (auto iter = table_space_map_.begin(); table_space_map_.end() != iter; ++iter) {
      MOD_DELETE_OBJECT(TableSpace, iter->second); 
    }
    table_space_id_ = 0;
    table_space_map_.clear();
    wait_remove_table_space_map_.clear();
    extent_io_info_map_.clear();
    garbage_files_.clear();
    is_inited_ = false;
  }
}

ExtentSpaceManager &ExtentSpaceManager::get_instance()
{
  static ExtentSpaceManager extent_space_manager;
  return extent_space_manager;
}

int ExtentSpaceManager::init(util::Env *env, const util::EnvOptions &env_options, const common::DBOptions &db_options)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "ExtentSpaceManager has been inited", K(ret));
  } else if (IS_NULL(env)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret));
  } else {
    env_ = env;
    env_options_ = env_options;
    env_options_.use_direct_reads = true;
    env_options_.use_direct_writes = true;
    db_options_ = db_options;
    is_inited_ = true;
  }

  return ret;
}

int ExtentSpaceManager::create_table_space(const int64_t table_space_id)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(table_space_id < 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id));
  } else {
    SpinWLockGuard w_guard(table_space_lock_);
    if (FAILED(internal_create_table_space(table_space_id))) {
      SE_LOG(WARN, "fail to create table space", K(table_space_id));
    } else {
      SE_LOG(INFO, "success to create table space", K(table_space_id));
    }
  }

  return ret;
}

int ExtentSpaceManager::open_table_space(const int64_t table_space_id)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(table_space_id < 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id));
  } else {
    SpinWLockGuard w_guard(table_space_lock_);
    if (table_space_map_.end() != table_space_map_.find(table_space_id)) {
      SE_LOG(INFO, "table space has opened", K(table_space_id));
    } else if (FAILED(internal_create_table_space(table_space_id))) {
      SE_LOG(WARN, "fail to internal create table space", K(ret), K(table_space_id));
    } else {
      update_max_table_space_id(table_space_id);
      SE_LOG(INFO, "success to open table space", K(table_space_id));
    }
  }
    
  return ret;
}

int ExtentSpaceManager::recycle_dropped_table_space()
{
  int ret = Status::kOk;
  int64_t table_space_id = 0;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else {
    SpinWLockGuard w_guard(table_space_lock_);
    for (auto iter = wait_remove_table_space_map_.begin(); SUCCED(ret) && wait_remove_table_space_map_.end() != iter;) {
      table_space_id = iter->first;
      if (IS_NULL(table_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, TableSpace must not nullptr", K(ret), K(table_space_id));
      } else {
        if (table_space->is_free()) {
          if (FAILED(table_space->remove())) {
            SE_LOG(WARN, "fail to remove table space", K(ret), K(table_space_id));
          } else if (1 != table_space_map_.erase(table_space_id)) {
            ret = Status::kErrorUnexpected;
            SE_LOG(WARN, "unexpected error, fail to erase table space", K(ret), K(table_space_id));
          } else {
            MOD_DELETE_OBJECT(TableSpace, table_space);
            iter = wait_remove_table_space_map_.erase(iter);
            SE_LOG(INFO, "success to recycle dropped table space", K(table_space_id));
          }
        } else {
          ++iter;
          SE_LOG(INFO, "TableSpace not free, can't recycle", K(table_space_id));
        }
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::register_subtable(const int64_t table_space_id, const int64_t index_id)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  SpinRLockGuard r_guard(table_space_lock_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret), K(table_space_id), K(index_id));
  } else if (UNLIKELY(table_space_id < 0) || UNLIKELY(index_id < 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id), K(index_id));
  } else if (IS_NULL(table_space = get_table_space(table_space_id))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, tablespace should not been nullptr", K(ret), K(table_space_id), K(index_id));
  } else if (FAILED(table_space->register_subtable(index_id))) {
    SE_LOG(WARN, "fail to unregister subtable", K(ret), K(table_space_id), K(index_id));
  }

  return ret;
}

int ExtentSpaceManager::unregister_subtable(const int64_t table_space_id, const int64_t index_id)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  SpinWLockGuard w_guard(table_space_lock_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret), K(table_space_id), K(index_id));
  } else if (UNLIKELY(table_space_id < 0) || UNLIKELY(index_id < 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalaid argument", K(ret), K(table_space_id), K(index_id));
  } else if (IS_NULL(table_space = get_table_space(table_space_id))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, tablespace should not been nullptr", K(ret), K(table_space_id), K(index_id));
  } else if (FAILED(table_space->unregister_subtable(index_id))) {
    SE_LOG(WARN, "fail to register subtable", K(ret), K(table_space_id), K(index_id));
  } else {
    if (table_space->is_dropped()) {
      if (!(wait_remove_table_space_map_.emplace(table_space_id, table_space).second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, fail to emplace to remove table space map", K(ret), K(table_space_id), K(index_id));
      } else {
        SE_LOG(INFO, "the table space waiting to remove", K(table_space_id), K(index_id));
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::allocate(const int64_t table_space_id,
                                 const int32_t extent_space_type,
                                 const std::string prefix,
                                 IOExtent *&extent)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;
  ExtentIOInfo io_info;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(table_space_id < 0) ||
             UNLIKELY(!is_valid_extent_space_type(extent_space_type))) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id), K(extent_space_type));
  } else {
#ifndef NDEBUG
    TEST_SYNC_POINT("ExtentSpaceManager::allocate::inject_allocate_hang");
#endif
    {
      SpinRLockGuard r_guard(table_space_lock_);
      if (IS_NULL(table_space = get_table_space(table_space_id))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, fail to find tablespace", K(ret), K(table_space_id), K(extent_space_type));
      } else if (FAILED(table_space->allocate(extent_space_type, prefix, io_info))) {
        SE_LOG(WARN, "fail to allocate extent", K(ret), K(table_space_id), K(extent_space_type));
      }
    }
    
    if (SUCCED(ret)) {
      SpinWLockGuard w_guard(io_info_map_lock_);
      if (FAILED(init_extent(io_info, extent))) {
        SE_LOG(WARN, "fail to init extent", K(ret), K(io_info));
      } else if (!(extent_io_info_map_.emplace(io_info.extent_id_.id(), io_info).second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, fail to emplace to io_info map", K(ret), K(io_info), K(table_space_id), K(extent_space_type));
      }
      assert(Status::kOk == ret);
    }

    if (SUCCED(ret)) {
      SE_LOG(INFO, "success to allocate extent", K(io_info), K(table_space_id), K(extent_space_type));
    }
  }

  return ret;
}

int ExtentSpaceManager::recycle(const int64_t table_space_id,
                                const int32_t extent_space_type,
                                const std::string prefix,
                                const ExtentId extent_id,
                                bool has_meta)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(table_space_id < 0) || UNLIKELY(!is_valid_extent_space_type(extent_space_type))) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
  } else {
    if (SUCCED(ret)) {
#ifndef NDEBUG
      TEST_SYNC_POINT("ExtentSpaceManager::recycle::inject_recycle_io_info_hang");
#endif
      SpinWLockGuard w_guard(io_info_map_lock_);
      if (1 != (extent_io_info_map_.erase(extent_id.id()))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, fail to erase from io_info map", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
      }
    }

    if (SUCCED(ret)) {
      if (has_meta && FAILED(ExtentMetaManager::get_instance().recycle_meta(extent_id))) {
        SE_LOG(WARN, "fail to recycle extent meta", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
      } else {
        SE_LOG(INFO, "success to recycle extent", K(table_space_id), K(extent_space_type), K(extent_id));
      }
    }

    /**after recycle from table space, the extent is allocatable. so recycle from table space after recycle
    from extent_io_info_map and extent_meta_mgr_*/
    if (SUCCED(ret)) {
      SpinRLockGuard r_guard(table_space_lock_);
      if (IS_NULL(table_space = get_table_space(table_space_id))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, tablespace should not nullptr",
            K(ret), K(table_space_id), K(extent_space_type), K(prefix), K(extent_id));
      } else if (FAILED(table_space->recycle(extent_space_type, prefix, extent_id))) {
        SE_LOG(WARN, "fail to recycle extent", K(ret), K(table_space_id),
            K(extent_space_type), K(prefix), K(extent_id));
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::reference(const int64_t table_space_id,
                                  const int32_t extent_space_type,
                                  const std::string prefix,
                                  const ExtentId extent_id)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;
  ExtentIOInfo io_info;
  bool extent_existed = false;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(table_space_id < 0) || UNLIKELY(!is_valid_extent_space_type(extent_space_type))) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
    abort();
  } else if (IS_NULL(table_space = get_table_space(table_space_id))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to find tablespace", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
  } else if (FAILED(table_space->reference_if_need(extent_space_type, prefix, extent_id, io_info, extent_existed))) {
    SE_LOG(WARN, "fail to reference extent", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
  } else if (extent_existed) {
    const auto &iter = extent_io_info_map_.find(extent_id.id());
    if (iter == extent_io_info_map_.end()) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, extent not found", K(ret), K(table_space_id), K(extent_space_type), K(extent_id));
    } else {
      // existed
    }
  } else if (!(extent_io_info_map_.emplace(extent_id.id(), io_info).second)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to emplace to io_info map",
        K(ret), K(table_space_id), K(extent_space_type), K(io_info));
  } else {
    SE_LOG(INFO, "success to reference extent", K(table_space_id), K(extent_space_type), K(extent_id));
  }

  return ret;
}

bool ExtentSpaceManager::TEST_find_extent(const ExtentId extent_id)
{
  SpinRLockGuard r_guard(io_info_map_lock_);
  auto iter = extent_io_info_map_.find(extent_id.id());
  return extent_io_info_map_.end() != iter;
}

int ExtentSpaceManager::get_readable_extent(ExtentId extent_id, IOExtent *&readable_extent)
{
  int ret = Status::kOk;
  ExtentIOInfo io_info;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else {
    SpinRLockGuard r_guard(io_info_map_lock_);
    auto iter = extent_io_info_map_.find(extent_id.id());
    if (extent_io_info_map_.end() == iter) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, the extent not exist", K(ret), K(extent_id));
    } else if (FAILED(init_extent(iter->second, readable_extent))) {
      SE_LOG(WARN, "fail to init readable extent", K(ret), K(io_info));
    } else {
      SE_LOG(DEBUG, "success to get readable extent", K(extent_id));
    }
  }

  return ret;
}

int ExtentSpaceManager::get_shrink_infos(const ShrinkCondition &shrink_condition, std::vector<ShrinkInfo> &shrink_infos)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(!shrink_condition.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(shrink_condition));
  } else {
    SpinRLockGuard r_guard(table_space_lock_);
    for (auto iter = table_space_map_.begin(); SUCCED(ret) && table_space_map_.end() != iter; ++iter) {
      if (IS_NULL(table_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, TableSpace must not nullptr", K(ret), "table_space_id", iter->first);
      } else if (FAILED(table_space->get_shrink_info_if_need(shrink_condition, shrink_infos))) {
        SE_LOG(WARN, "fail to get shrink info", K(ret), "table_space_id", iter->first);
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::get_shrink_infos(const int64_t table_space_id,
                                         const ShrinkCondition &shrink_condition,
                                         std::vector<ShrinkInfo> &shrink_infos)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(!shrink_condition.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argment", K(ret), K(shrink_condition));
  } else {
    SpinRLockGuard r_guard(table_space_lock_);
    if (IS_NULL(table_space = get_table_space(table_space_id))) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, fail to find tablespace", K(ret));
    } else if (FAILED(table_space->get_shrink_info_if_need(shrink_condition, shrink_infos))) {
      SE_LOG(WARN, "fail to get shrink info", K(ret), K(table_space_id));
    } else {
      SE_LOG(INFO, "success to get shrink info", K(table_space_id), K(shrink_condition));
    }
  }

  return ret;
}

int ExtentSpaceManager::get_shrink_info(const int64_t table_space_id,
                                        const int32_t extent_space_type,
                                        const ShrinkCondition &shrink_condition,
                                        ShrinkInfo &shrink_info)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(!shrink_condition.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret),
        K(table_space_id), K(extent_space_type), K(shrink_condition));
  } else {
    SpinRLockGuard r_guard(table_space_lock_);
    if (IS_NULL(table_space = get_table_space(table_space_id))) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, fail to find tablespace", K(ret),
          K(table_space_id), K(extent_space_type), K(shrink_condition));
    } else if (FAILED(table_space->get_shrink_info(extent_space_type,
                                                   shrink_condition,
                                                   shrink_info))) {
      SE_LOG(WARN, "fail to get shrink info", K(ret),
          K(table_space_id), K(extent_space_type), K(shrink_condition));
    } else {
      SE_LOG(INFO, "success to get shrink info",
          K(table_space_id), K(extent_space_type), K(shrink_condition));
    }
  }

  return ret;
}

int ExtentSpaceManager::move_extens_to_front(const ShrinkInfo &shrink_info, std::unordered_map<int64_t, ExtentIOInfo> &replace_map)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(!shrink_info.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(shrink_info));
  } else {
    {
      SpinRLockGuard r_guard(table_space_lock_);
      if (IS_NULL(table_space = get_table_space(shrink_info.table_space_id_))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, TableSpace must not nullptr", K(ret), K(shrink_info));
      } else if (FAILED(table_space->move_extens_to_front(shrink_info, replace_map))) {
        SE_LOG(WARN, "fail to move extents to front", K(ret), K(shrink_info));
      }
    }

    if (SUCCED(ret)) {
      SpinWLockGuard w_guard(io_info_map_lock_);
      for (auto iter = replace_map.begin(); SUCCED(ret) && replace_map.end() != iter; ++iter) {
        const ExtentIOInfo &io_info = iter->second;
        if (!(extent_io_info_map_.emplace(io_info.extent_id_.id(), io_info).second)) {
          ret = Status::kErrorUnexpected;
          SE_LOG(WARN, "unexpected error, fail to emplace to io_info map", K(ret), K(io_info));
        }
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::shrink_extent_space(const ShrinkInfo &shrink_info)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  SpinRLockGuard r_guard(table_space_lock_);
  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (IS_NULL(table_space = get_table_space(shrink_info.table_space_id_))) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "fail to get table space", K(ret), K(shrink_info));
  } else if (FAILED(table_space->shrink(shrink_info))) {
    SE_LOG(WARN, "fail to shrink extent space", K(ret), K(shrink_info));
  } else {
    SE_LOG(INFO, "success to shrink extent space", K(shrink_info));
  }

  return ret;
}

int ExtentSpaceManager::get_data_file_stats(std::vector<DataFileStatistics> &data_file_stats)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "unexpected error, ExtentSpaceManager should been inited first", K(ret));
  } else {
    SpinRLockGuard r_guard(table_space_lock_);
    for (auto iter = table_space_map_.begin(); SUCCED(ret) && table_space_map_.end() != iter; ++iter) {
      if (IS_NULL(table_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, TableSpace must not nullptr", K(ret), "table_space_id", iter->first);
      } else if (FAILED(table_space->get_data_file_stats(data_file_stats))) {
        SE_LOG(WARN, "fail to get data file stats", K(ret), "table_space_id", iter->first);
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::get_data_file_stats(const int64_t table_space_id,
                                            std::vector<DataFileStatistics> &data_file_stats)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "unexpected error, ExtentSpaceManager should been inited first", K(ret));
  } else if (UNLIKELY(table_space_id < 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(table_space_id));
  } else {
    SpinRLockGuard r_guard(table_space_lock_);
    auto iter = table_space_map_.find(table_space_id);
    if (table_space_map_.end() == iter) {
      ret = Status::kNotFound;
      SE_LOG(WARN, "the table space not found", K(ret), K(table_space_id));
    } else if (IS_NULL(table_space = iter->second)) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, TableSpace must not nullptr", K(ret), K(table_space_id));
    } else if (FAILED(table_space->get_data_file_stats(data_file_stats))) {
      SE_LOG(WARN, "fail to get data file stats", K(ret), K(table_space_id));
    } else {
      SE_LOG(DEBUG, "success to get data file stats", K(table_space_id));
    }
  }

  return ret;
}

int ExtentSpaceManager::open_all_data_file()
{
  int ret = Status::kOk;
  std::string dir_path;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < db_options_.db_paths.size(); ++i) {
      if (i == OBJECT_EXTENT_SPACE) {
        // there is no file in object extent space.
        continue;
      }
      dir_path = db_options_.db_paths.at(i).path;
      SE_LOG(INFO, "open datafiles in directory", K(dir_path));
      if (FAILED(open_specific_directory(dir_path))) {
        SE_LOG(WARN, "fail to open specific directory", K(ret), K(dir_path));
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::rebuild()
{
  int ret = Status::kOk;
  int64_t table_space_id = 0;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else {
    SpinRLockGuard r_guard(table_space_lock_);
    for (auto iter = table_space_map_.begin(); SUCCED(ret) && table_space_map_.end() != iter; ++iter) {
      table_space_id = iter->first;
      if (IS_NULL(table_space = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpected error, tablespace must not nullptr", K(ret), K(table_space_id));
      } else if (FAILED(table_space->rebuild())) {
        SE_LOG(WARN, "fail to rebuild table space", K(ret), K(table_space_id));
      } else {
        SE_LOG(DEBUG, "success to rebuild tablespace", K(table_space_id));
      }
    }

    if (SUCCED(ret)) {
      if (FAILED(ExtentMetaManager::get_instance().clear_free_extent_meta())) {
        SE_LOG(WARN, "fail to clear free extent meta", K(ret));
      } else if (FAILED(clear_garbage_files())) {
        SE_LOG(WARN, "fail to clear garbage files", K(ret));
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::internal_create_table_space(const int64_t table_space_id)
{
  int ret = Status::kOk;
  TableSpace *table_space = nullptr;
  CreateTableSpaceArgs args(table_space_id, db_options_.db_paths);

  if (UNLIKELY(!args.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(args));
  } else if (IS_NULL(table_space = MOD_NEW_OBJECT(ModId::kExtentSpaceMgr,
                                                  TableSpace,
                                                  env_,
                                                  env_options_))) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "fail to allocate memory for TableSpace", K(ret));
  } else if (FAILED(table_space->create(args))) {
    SE_LOG(WARN, "fail to create table space", K(ret), K(args));
  } else if (!(table_space_map_.emplace(table_space_id, table_space).second)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "unexpected error, fail to emplace table space", K(ret), K(args));
  }

  return ret;
}

TableSpace *ExtentSpaceManager::get_table_space(int64_t table_space_id)
{
  TableSpace *table_space = nullptr;
  auto iter = table_space_map_.find(table_space_id);
  if (table_space_map_.end() != iter) {
    table_space = iter->second;
  }
  return table_space;
}

int ExtentSpaceManager::open_specific_directory(const std::string &dir_path)
{
  int ret = Status::kOk;
  std::vector<int64_t> data_file_numbers;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret), K(dir_path));
  } else if (FAILED(get_data_file_numbers(dir_path, data_file_numbers))) {
    SE_LOG(WARN, "fail to get datafile numers", K(ret), K(dir_path));
  } else if (FAILED(open_data_files(dir_path, data_file_numbers))) {
    SE_LOG(ERROR, "fail to open datafiles", K(ret), K(dir_path));
  } else {
    SE_LOG(INFO, "success to open specific directory", K(dir_path));
  }

  return ret;
}

int ExtentSpaceManager::get_data_file_numbers(const std::string &dir_path,
                                              std::vector<int64_t> &data_file_numbers)
{
  int ret = Status::kOk;
  std::string file_name;
  std::vector<std::string> data_file_names;
  int64_t file_number = 0;
  util::FileType file_type = util::kInvalidFileType;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret));
  } else if (FAILED(env_->GetChildren(dir_path, &data_file_names).code())) {
    SE_LOG(WARN, "fail to get data file names", K(ret), K(dir_path));
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < data_file_names.size(); ++i) {
      file_name = data_file_names.at(i);
      // Ignore the file that we can't recognize it.
      if ((Status::kOk == FileNameUtil::parse_file_name(file_name, file_number, file_type)) && util::kDataFile == file_type) {
        data_file_numbers.push_back(file_number);
        SE_LOG(INFO, "find data file", K(file_number), K(file_name));
      } else {
        SE_LOG(INFO, "other type file", K(file_name));
      }
    }
  }

  return ret;
}

int ExtentSpaceManager::open_data_files(const std::string &dir_path, const std::vector<int64_t> &data_file_numbers)
{
  int ret = Status::kOk;
  std::string file_name;
  int64_t file_number = 0;
  DataFile *data_file = nullptr;
  TableSpace *table_space = nullptr;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ExtentSpaceManager should been inited first", K(ret), K(dir_path));
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < data_file_numbers.size(); ++i) {
      file_number = data_file_numbers.at(i);
      file_name = util::FileNameUtil::data_file_path(dir_path, file_number);
      if (IS_NULL(data_file = MOD_NEW_OBJECT(ModId::kExtentSpaceMgr,
                                             DataFile,
                                             env_,
                                             env_options_))) {
        ret = Status::kMemoryLimit;
        SE_LOG(WARN, "fail to allocate memory for DataFile", K(ret), K(dir_path), K(file_number), K(file_name));
      } else if (FAILED(data_file->open(file_name))) {
        if (data_file->is_garbage_file()) {
          garbage_files_.push_back(file_name);
          SE_LOG(INFO, "find garbage data file", K(file_name));
          //over write ret
          ret = Status::kOk;
        } else {
          SE_LOG(WARN, "fail to open data file", K(ret), K(dir_path), K(file_number), K(file_name));
        }
      } else if (IS_NULL(table_space = get_table_space(data_file->get_table_space_id()))) {
        SE_LOG(WARN, "the table space may have been removed, ignore it. the datafile can been manual delete", K(ret),
            "table_space_id", data_file->get_table_space_id(), K(file_number), K(file_name));
        MOD_DELETE_OBJECT(DataFile, data_file);
      } else if (FAILED(table_space->add_data_file(data_file))) {
        SE_LOG(ERROR, "fail to add data file to tablespace", K(ret), K(file_name));
      } else {
        SE_LOG(INFO, "success to add datafile", K(file_number), K(file_name));
      }
      update_max_file_number(file_number + 1);
    }
  }

  return ret;
}

void ExtentSpaceManager::update_max_file_number(const int64_t file_number)
{
  if (file_number > FileNumberAllocator::get_instance().get()) {
    FileNumberAllocator::get_instance().set(file_number);
  }
}

void ExtentSpaceManager::update_max_table_space_id(const int64_t table_space_id)
{
  if (table_space_id > table_space_id_.load()) {
    table_space_id_.store(table_space_id);
  }
}

int ExtentSpaceManager::clear_garbage_files()
{
  int ret = Status::kOk;

  for (uint32_t i = 0; SUCCED(ret) && i < garbage_files_.size(); ++i) {
    if (FAILED(env_->DeleteFile(garbage_files_.at(i)).code())) {
      SE_LOG(WARN, "fail to delete garbage file", K(ret), "file_path", garbage_files_.at(i));
    } else {
      SE_LOG(INFO, "success to delete garbage file", "file_path", garbage_files_.at(i));
    }
  }

  garbage_files_.clear();
  return ret;
}

int ExtentSpaceManager::init_extent(const ExtentIOInfo &io_info, IOExtent *&extent)
{
  int ret = Status::kOk;

  if (UNLIKELY(!io_info.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(io_info));
  } else if (FILE_EXTENT_SPACE == io_info.type_) {
    FileIOExtent *file_extent = nullptr;
    if (IS_NULL(file_extent = NEW_OBJECT(ModId::kIOExtent, FileIOExtent))) {
      ret = Status::kMemoryLimit;
      SE_LOG(WARN, "fail to allocate memory for FileIOExtent", K(ret));
    } else if (FAILED(file_extent->init(io_info.extent_id_, io_info.unique_id_, io_info.fd_))) {
      SE_LOG(WARN, "fail to init file extent", K(ret), K(io_info));
    } else {
      extent = file_extent;
    }

    // resource clean
    if (FAILED(ret)) {
      DELETE_OBJECT(ModId::kIOExtent, file_extent);
    }
  } else if (OBJECT_EXTENT_SPACE == io_info.type_) {
    ObjectIOExtent *object_extent = nullptr;
    if (IS_NULL(object_extent = NEW_OBJECT(ModId::kIOExtent, ObjectIOExtent))) {
      ret = Status::kMemoryLimit;
      SE_LOG(WARN, "fail to allocate memory for ObjectIOExtent", K(ret));
    } else if (FAILED(object_extent->init(io_info.extent_id_,
                                          io_info.unique_id_,
                                          io_info.object_store_,
                                          io_info.bucket_,
                                          io_info.prefix_))) {
      SE_LOG(WARN, "fail to init object extent", K(ret), K(io_info));
    } else {
      extent = object_extent;
    }

    // resource clean
    if (FAILED(ret)) {
      DELETE_OBJECT(ModId::kIOExtent, object_extent);
    }
  } else {
    se_assert(false);
  }

  return ret;
}

} //namespace storage
} //namespace smartengine

