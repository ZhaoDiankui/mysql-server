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

#include "storage/shrink_job.h"
#include "db/db_impl.h"
#include "db/version_set.h"
#include "storage/extent_meta_manager.h"
#include "storage/storage_logger.h"

namespace smartengine
{
using namespace common;
namespace storage
{
ShrinkJob::ShrinkJob()
    : is_inited_(false),
      mutex_(nullptr),
      global_ctx_(nullptr),
      shrink_info_()
{
}

ShrinkJob::~ShrinkJob()
{
}

int ShrinkJob::init(monitor::InstrumentedMutex *mutex,
                    db::GlobalContext *global_ctx,
                    const ShrinkInfo &shrink_info)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "ShrinkJob has been inited", K(ret));
  } else if (IS_NULL(mutex)
             || IS_NULL(global_ctx) 
             || UNLIKELY(!shrink_info.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), KP(mutex), KP(global_ctx), K(shrink_info));
  } else {
    mutex_ = mutex;
    global_ctx_ = global_ctx;
    shrink_info_ = shrink_info;
    is_inited_ = true;
  }

  return ret;
}

int ShrinkJob::run()
{
  int ret = Status::kOk;
  bool can_shrink = true;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ShrinkJob should been inited first", K(ret));
  } else if (FAILED(before_shrink(can_shrink))) {
    SE_LOG(WARN, "fail to prepare for shrink", K(ret));
  } else {
    if (can_shrink) {
      if (FAILED(do_shrink())) {
        SE_LOG(WARN, "fail to do shrink", K(ret));
      }
      /**if can_shrink is true, after_shrink should execute anyway
       * because unref the subtable and reset pending_shrink must been done*/
      after_shrink();
    } else {
      SE_LOG(INFO, "the shrink job can't run");
    }
  }

  return ret;
}

int ShrinkJob::before_shrink(bool &can_shrink)
{
  int ret = Status::kOk;
  db::SubTable *sub_table = nullptr;

  mutex_->Lock();
  db::AllSubTableGuard all_subtable_guard(global_ctx_);
  const db::SubTableMap &subtable_map = all_subtable_guard.get_subtable_map();
  /**check confict with other task like flush, compaction, recycle...*/
  for (auto iter = shrink_info_.index_id_set_.begin();
       SUCCED(ret) && shrink_info_.index_id_set_.end() != iter;
       ++iter) {
    auto subtable_iter = subtable_map.find(*iter);
    if (subtable_map.end() == subtable_iter) {
      SE_LOG(INFO, "the subtable may been droppen, stop shrink the extent space", "index_id", *iter);
      can_shrink = false;
      break;
    } else if (IS_NULL(sub_table = subtable_iter->second)) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, subtable must not nullptr", K(ret), "index_id", *iter);
    } else if (!sub_table->can_shrink()) {
      SE_LOG(INFO, "subtable can't shrink", "index_id", *iter);
      sub_table->print_internal_stat();
      can_shrink = false;
      break;
    }
  }

  /**check ShrinkInfo changed since the ShrinkJob generated durin schedule*/
  if (SUCCED(ret) && can_shrink) {
    if (FAILED(double_check_shrink_info(can_shrink))) {
      SE_LOG(WARN, "fail to double check shrink info.", K(ret));
    }
  }

  /**do precheck success and can_shrink is true, set pending_shrink*/
  if (SUCCED(ret) && can_shrink) {
    for (auto iter = shrink_info_.index_id_set_.begin();
         SUCCED(ret) && shrink_info_.index_id_set_.end() != iter;
         ++iter) {
      auto subtable_iter = subtable_map.find(*iter);
      if (subtable_map.end() == subtable_iter) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "unexpect error, the subtable should exist", K(ret), "index_id", *iter);
      } else { 
        subtable_iter->second->Ref();
        subtable_iter->second->set_pending_shrink(true);
        subtable_map_.emplace(subtable_iter->first, subtable_iter->second);
      }
    }

    /**If failed, rollback the ref and pending_shrink.
    the next rollback operation must not failed*/
    if (FAILED(ret)) {
      for (auto iter = subtable_map_.begin();
           SUCCED(ret) && subtable_map_.end() != iter;
           ++iter) {
        if (IS_NULL(sub_table = iter->second)) {
          ret = Status::kErrorUnexpected;
          SE_LOG(WARN, "unexpected error, the subtable must not nullptr",
              K(ret), "index_id", iter->first);
        } else {
          sub_table->set_pending_shrink(false);
          if (sub_table->Unref()) {
            MOD_DELETE_OBJECT(ColumnFamilyData, sub_table);
            SE_LOG(INFO, "delete one subtable in shrink job", "index_id", iter->first);
          }
        }
      }
    }
  }

  mutex_->Unlock();

  return ret;
}

int ShrinkJob::do_shrink()
{
  int ret = Status::kOk;

  if (FAILED(move_extent())) {
    SE_LOG(WARN, "fail to move extent", K(ret));
  } else if (FAILED(install_shrink_result())) {
    SE_LOG(WARN, "fail to install shrink result", K(ret));
  } else if (FAILED(shrink_physical_space())) {
    SE_LOG(WARN, "fail to shrink physical space", K(ret));
  } else {
    SE_LOG(INFO, "success to do shrink", K_(shrink_info));
  }

  return ret;
}

int ShrinkJob::after_shrink()
{
  int ret = Status::kOk;
  mutex_->Lock();
  for (auto iter = subtable_map_.begin(); subtable_map_.end() != iter; ++iter) {
    iter->second->set_pending_shrink(false);
    if (iter->second->Unref()) {
      MOD_DELETE_OBJECT(ColumnFamilyData, iter->second);
    }
  }
  mutex_->Unlock();

  return ret;
}

int ShrinkJob::move_extent()
{
  int ret = Status::kOk;

  if (FAILED(get_extent_positions())) {
    SE_LOG(WARN, "fail to get extent infos", K(ret));
  } else if (FAILED(ExtentSpaceManager::get_instance().move_extens_to_front(shrink_info_, extent_replace_map_))) {
    SE_LOG(WARN, "fail to move extents to front", K(ret));
  }
  
  return ret;
}

int ShrinkJob::install_shrink_result()
{
  int ret = Status::kOk;
  int64_t dummy_commit_seq = 0;

  if (FAILED(StorageLogger::get_instance().begin(SeEvent::SHRINK_EXTENT_SPACE))) {
    SE_LOG(WARN, "fail to begin shrink trans", K(ret));
  } else if (FAILED(write_extent_metas())) {
    SE_LOG(WARN, "fail to write extent metas", K(ret));
  } else if (FAILED(apply_change_infos())) {
    SE_LOG(WARN, "fail to apply change infos", K(ret));
  } else if (FAILED(StorageLogger::get_instance().commit(dummy_commit_seq))) {
    SE_LOG(WARN, "fail to commit shrink trans", K(ret));
  } else if (FAILED(update_super_version())) {
    SE_LOG(WARN, "fail to update super version", K(ret));
  } else {
    SE_LOG(INFO, "success to install shrink result", K(ret));
  }

  return ret;
}

int ShrinkJob::write_extent_metas()
{
  int ret = Status::kOk;
  ExtentId old_extent_id;
  ExtentId new_extent_id;
  ExtentMeta *old_extent_meta = nullptr;

  for (auto extent_iter = extent_replace_map_.begin();
       SUCCED(ret) && extent_replace_map_.end() != extent_iter; ++extent_iter) {
    old_extent_id = extent_iter->first;
    new_extent_id = extent_iter->second.extent_id_;
    auto extent_position_iter = extent_positions_.find(old_extent_id.id());
    if (extent_positions_.end() == extent_position_iter) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, fail to find old extent info", K(ret), K(old_extent_id), K(new_extent_id));
    }

    if (SUCCED(ret)) {
      //step1: write new extent meta
      if (IS_NULL(old_extent_meta = ExtentMetaManager::get_instance().get_meta(old_extent_id))) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "fail to get extent meta", K(ret), K(old_extent_id), K(new_extent_id));
      } else {
        ExtentMeta new_extent_meta(*old_extent_meta);
        new_extent_meta.extent_id_ = new_extent_id;
        if (FAILED(ExtentMetaManager::get_instance().write_meta(new_extent_meta, true /**write_log*/))) {
          SE_LOG(WARN, "fail to write meta", K(ret), K(old_extent_id), K(new_extent_id), K(*old_extent_meta));
        }
      }
    }

    if (SUCCED(ret)) {
      //step2: build change info
      const ExtentPosition &extent_position = extent_position_iter->second;
      auto change_info_iter = change_info_map_.find(extent_position.index_id_);
      if (change_info_map_.end() == change_info_iter) {
        if (!(change_info_map_.emplace(extent_position.index_id_, ChangeInfo()).second)) {
          SE_LOG(WARN, "fail to emplace changeinfo", K(ret), K(extent_position));
        } else {
          change_info_iter = change_info_map_.find(extent_position.index_id_);
        }
      }

      if (SUCCED(ret)) {
        if (FAILED(change_info_iter->second.replace_extent(extent_position.layer_position_, old_extent_id, new_extent_id))) {
          SE_LOG(WARN, "fail to replace extent", K(ret), K(extent_position), K(old_extent_id), K(new_extent_id));
        }
      }
    }
  }

  return ret;
}

int ShrinkJob::apply_change_infos()
{
  int ret = Status::kOk;
  db::SubTable *sub_table = nullptr;
  db::SuperVersion *old_version = nullptr;

  for (auto iter = change_info_map_.begin();
       SUCCED(ret) && change_info_map_.end() != iter; ++iter) {
    auto subtable_iter = subtable_map_.find(iter->first);
    if (subtable_map_.end() == subtable_iter) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, fail to find subtable", K(ret));
    } else if (IS_NULL(sub_table = subtable_iter->second)) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, subtable must not nullptr", K(ret));
    } else if (FAILED(sub_table->apply_change_info(iter->second, true/*write_log*/))) {
      SE_LOG(WARN, "fail to apply change info", K(ret));
    } else {
      mutex_->Lock();
      old_version = sub_table->InstallSuperVersion(MOD_NEW_OBJECT(memory::ModId::kSuperVersion, db::SuperVersion), mutex_);
      mutex_->Unlock();
      if (nullptr != old_version) {
        MOD_DELETE_OBJECT(SuperVersion, old_version);
      }
    }
  }

  return ret;
}

int ShrinkJob::update_super_version()
{
  int ret = Status::kOk;
  db::SubTable *sub_table = nullptr;
  db::SuperVersion *old_version = nullptr;

  for (auto iter = change_info_map_.begin();
       SUCCED(ret) && change_info_map_.end() != iter; ++iter) {
    auto subtable_iter = subtable_map_.find(iter->first);
    if (subtable_map_.end() == subtable_iter) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, fail to find subtable", K(ret));
    } else if (IS_NULL(sub_table = subtable_iter->second)) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "unexpected error, subtable must not nullptr", K(ret));
    } else {
      mutex_->Lock();
      old_version = sub_table->InstallSuperVersion(MOD_NEW_OBJECT(memory::ModId::kSuperVersion, db::SuperVersion), mutex_);
      mutex_->Unlock();
      if (nullptr != old_version) {
        MOD_DELETE_OBJECT(SuperVersion, old_version);
      }
    }
  }

  return ret;
}
int ShrinkJob::shrink_physical_space()
{
  int ret = Status::kOk;
  bool can_shrink = can_physical_shrink();
  int32_t wait_times = 2;

  while (!can_shrink && wait_times > 0) {
    util::Env::Default()->SleepForMicroseconds(1000000);
    can_shrink = can_physical_shrink();
    wait_times--;
  }

  if (can_shrink) {
    if (FAILED(ExtentSpaceManager::get_instance().shrink_extent_space(shrink_info_))) {
      SE_LOG(WARN, "fail to shrink extent space", K(ret));
    } else {
      SE_LOG(INFO, "success to shrink extent space", K_(shrink_info));
    }
  } else {
    SE_LOG(INFO, "cant't do pyhsical shrink", K_(shrink_info));
  }

  return ret;
}

bool ShrinkJob::can_physical_shrink()
{
  bool can_shrink = true;
  for (auto iter = subtable_map_.begin(); can_shrink && subtable_map_.end() != iter; ++iter) {
    mutex_->Lock();
    if (!(iter->second->can_physical_shrink())) {
      can_shrink = false;
    }
    mutex_->Unlock();
  }
  return can_shrink;
}

int ShrinkJob::get_extent_positions()
{
  int ret = Status::kOk;

  for (auto iter = subtable_map_.begin(); SUCCED(ret) && subtable_map_.end() != iter; ++iter) {
    if (FAILED(iter->second->get_extent_positions(extent_positions_))) {
      SE_LOG(WARN, "fail to get extent infos", K(ret), "index_id", iter->first);
    }
  }

  return ret;
}

int ShrinkJob::double_check_shrink_info(bool &can_shrink)
{
  int ret = Status::kOk;
  ShrinkInfo current_shrink_info;

  if (FAILED(ExtentSpaceManager::get_instance().get_shrink_info(shrink_info_.table_space_id_,
                                                                shrink_info_.extent_space_type_,
                                                                shrink_info_.shrink_condition_,
                                                                current_shrink_info))) {
    SE_LOG(WARN, "fail to get shrink info", K(ret), K_(shrink_info));
  } else {
    if (shrink_info_ == current_shrink_info) {
      //do nothing
    } else {
      can_shrink = false;
      SE_LOG(INFO, "the shrink info has changed, cancel this shrink job", K_(shrink_info),
          K(current_shrink_info));
    }
  }

  return ret;
}

} //namespace storage
} //namespace smartengine
