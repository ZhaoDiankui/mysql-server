/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "backup/hotbackup_impl.h"
#include "memory/thread_local_store.h"
#include "storage/storage_logger.h"
#include "storage/storage_manager.h"
#include "util/file_util.h"
#include "util/sync_point.h"

namespace smartengine
{
using namespace common;
using namespace db;
using namespace storage;

namespace util
{

char se_backup_status[][16] = {"checkpoint",
                               "acquire",
                               "incremental",
                               "release",
                               ""};

BackupSnapshot *BackupSnapshot::get_instance() { return BackupSnapshotImpl::get_instance(); }

int BackupSnapshot::init(DB *db, const char *backup_tmp_dir_path)
{
  return Status::kNotSupported;
}

int BackupSnapshot::create_tmp_dir(DB *db) { return Status::kNotSupported; }

int BackupSnapshot::cleanup_tmp_dir(db::DB *db) { return Status::kNotSupported; }

int BackupSnapshot::lock_instance() { return Status::kNotSupported; }

int BackupSnapshot::unlock_instance() { return Status::kNotSupported; }

int BackupSnapshot::lock_one_step() { return Status::kNotSupported; }

int BackupSnapshot::unlock_one_step() { return Status::kNotSupported; }

int BackupSnapshot::check_lock_status() { return Status::kNotSupported; }

int BackupSnapshot::get_backup_status(const char*& status) {
  return Status::kNotSupported;
}

int BackupSnapshot::set_backup_status(const char* status) {
  return Status::kNotSupported;
}

int BackupSnapshot::do_checkpoint(DB *db, const char *backup_tmp_dir_path) { return Status::kNotSupported; }

int BackupSnapshot::accquire_backup_snapshot(DB *db, BackupSnapshotId *backup_id, db::BinlogPosition &binlog_pos)
{
  return Status::kNotSupported;
}

int BackupSnapshot::record_incremental_extent_ids(DB *db) { return Status::kNotSupported; }

int BackupSnapshot::release_old_backup_snapshot(DB *db, BackupSnapshotId backup_id)
{
  return Status::kNotSupported;
}

int BackupSnapshot::list_backup_snapshots(std::vector<BackupSnapshotId> &backup_ids) { return Status::kNotSupported; }

int BackupSnapshot::release_current_backup_snapshot(DB *db) { return Status::kNotSupported; }

BackupSnapshotId BackupSnapshot::current_backup_id() {
  return 0;
}

BackupSnapshotImpl::BackupSnapshotImpl()
    : process_tid_(free_tid_),
      first_manifest_file_num_(0),
      last_manifest_file_num_(0),
      last_manifest_file_size_(0),
      last_wal_file_num_(0),
      last_backup_id_(0),
      cur_backup_id_(0) {}

BackupSnapshotImpl::~BackupSnapshotImpl() {
  destroy();
}

void BackupSnapshotImpl::destroy() {
  // we can only clear this map when smartengine exit
  backup_snapshot_map_.clear();
}

BackupSnapshotImpl *BackupSnapshotImpl::get_instance() {
  static BackupSnapshotImpl instance;
  return &instance;
}

int BackupSnapshotImpl::try_exclusive_lock() {
  int ret = Status::kOk;
  int tid = memory::get_tc_tid();
  int free_tid = free_tid_;
  if (!process_tid_.compare_exchange_strong(free_tid, tid)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "There is another running backup job!", K(ret));
  }
  return ret;
}

int BackupSnapshotImpl::unlock_exclusive_lock() {
  process_tid_.store(free_tid_);
  return Status::kOk;
}

int BackupSnapshotImpl::lock_instance() {
  int ret = Status::kOk;
  if (SUCCED(try_exclusive_lock())) {
    instance_locked_ = true;
  }
  return ret;
}

int BackupSnapshotImpl::unlock_instance() {
  int ret = Status::kOk;
  if (instance_locked_ && SUCCED(unlock_exclusive_lock())) {
    instance_locked_ = false;
  }
  return ret;
}

int BackupSnapshotImpl::lock_one_step() { return try_exclusive_lock(); }

int BackupSnapshotImpl::unlock_one_step() { return unlock_exclusive_lock(); }

int BackupSnapshotImpl::check_lock_status() {
  int ret = Status::kOk;
  int tid = memory::get_tc_tid();
  int process_tid = process_tid_.load();
  if (free_tid_ == process_tid) {
    ret = Status::kNotInit;
    SE_LOG(INFO, "There is no backup job running", K(ret));
  } else if (tid != process_tid) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "There is another backup job still running!", K(ret), K(process_tid));
  }
  return ret;
}

int BackupSnapshotImpl::init(DB *db, const char *backup_tmp_dir_path) {
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else {
    first_manifest_file_num_ = 0;
    last_manifest_file_num_ = 0;
    last_manifest_file_size_ = 0;
    last_wal_file_num_ = 0;
    cur_backup_id_ = 0;
    backup_tmp_dir_path_ = (nullptr == backup_tmp_dir_path) ?
        db->GetName() + BACKUP_TMP_DIR : backup_tmp_dir_path;
    do_cleanup(db);
    backup_status_ = se_backup_status[4];
  }
  return ret;
}

void BackupSnapshotImpl::reset() {
  first_manifest_file_num_ = 0;
  last_manifest_file_num_ = 0;
  last_manifest_file_size_ = 0;
  last_wal_file_num_ = 0;
  cur_backup_id_ = 0;
  backup_tmp_dir_path_.clear();
  cur_meta_snapshots_.clear();
  backup_status_ = se_backup_status[4];
  if (instance_locked_) {
    unlock_instance();
  }
}

int BackupSnapshotImpl::get_backup_status(const char *&status) {
  status = backup_status_;  
  return Status::kOk;
}

int BackupSnapshotImpl::set_backup_status(const char *status) {
  backup_status_ = status;
  return Status::kOk;
}

BackupSnapshotId BackupSnapshotImpl::current_backup_id()
{
  return cur_backup_id_;
}

BackupSnapshotId BackupSnapshotImpl::generate_backup_id()
{
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  BackupSnapshotId snapshot_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

  if (last_backup_id_ == 0) {
    last_backup_id_ = backup_snapshot_map_.get_latest_backup_id();
  }

  if (last_backup_id_ >= snapshot_time_ms) {
      snapshot_time_ms = last_backup_id_ + 1;
  }
  return snapshot_time_ms;
}

int BackupSnapshotImpl::do_checkpoint(DB *db, const char *backup_tmp_dir_path) {
  int ret = Status::kOk;
  SSTFileChecker sst_file_checker;

  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (FAILED(check_lock_status())) {
    SE_LOG(WARN, "Failed to check lock status", K(ret));
  } else if (backup_snapshot_map_.get_backup_snapshot_count() >= db::BackupSnapshotMap::kMaxBackupSnapshotNum) {
    ret = Status::kOverLimit;
    SE_LOG(WARN, "backup snapshot count reach the max limit, please release some old backup snapshots first", K(ret),
           "max backup snapshot num", db::BackupSnapshotMap::kMaxBackupSnapshotNum);
  } else if (FAILED(init(db, backup_tmp_dir_path))) {
    SE_LOG(WARN, "Failed to init backup snapshot", K(ret));
  } else if (FAILED(db->do_manual_checkpoint(first_manifest_file_num_))) {
    SE_LOG(WARN, "Failed to do manual checkpoint", K(ret));
  } else if (FAILED(create_tmp_dir(db))) {
    SE_LOG(WARN, "Failed to create tmp dir", K(ret));
  } else if (FAILED(link_files(db, (CurrentFileChecker *)nullptr, &sst_file_checker, (WalDirFileChecker *)nullptr))) {
    SE_LOG(WARN, "Failed to link sst files", K(ret));
  } else {
    SE_LOG(INFO, "Success to do checkpoint", K(ret), K(first_manifest_file_num_));
  }

  if (FAILED(ret) && Status::kInitTwice != ret) {
    do_cleanup(db);
    reset();
  }

  return ret;
}

int BackupSnapshotImpl::accquire_backup_snapshot(DB *db, BackupSnapshotId *backup_id, db::BinlogPosition &binlog_pos)
{
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (IS_NULL(backup_id)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "backup id is nullptr", K(ret));
  } else if (FAILED(check_lock_status())) {
    SE_LOG(WARN, "Failed to check lock status", K(ret));
  } else if (backup_snapshot_map_.get_backup_snapshot_count() >= db::BackupSnapshotMap::kMaxBackupSnapshotNum) {
    ret = Status::kOverLimit;
    SE_LOG(WARN,
           "backup snapshot count reach the max limit, please release some old backup snapshots first",
           K(ret),
           "max backup snapshot num",
           db::BackupSnapshotMap::kMaxBackupSnapshotNum);
  } else {
    *backup_id = generate_backup_id();
    last_backup_id_ = *backup_id;
    cur_backup_id_ = *backup_id;

    db->DisableFileDeletions();

    CurrentFileChecker current_file_checker;
    if (FAILED(link_files(db, &current_file_checker, (DataDirFileChecker *)nullptr, (WalDirFileChecker *)nullptr))) {
      SE_LOG(WARN, "Failed to link current file", K(ret));
    } else if (FAILED(db->create_backup_snapshot(cur_backup_id_,
                                                 cur_meta_snapshots_,
                                                 last_manifest_file_num_,
                                                 last_manifest_file_size_,
                                                 last_wal_file_num_,
                                                 last_binlog_pos_))) {
      SE_LOG(WARN, "Failed to acquire backup snapshot", K(ret));
    } else {
#ifndef NDEBUG
      TEST_SYNC_POINT_CALLBACK("BackupSnapshotImpl::acquire_snapshot::after_create_backup_snapshot", db);
#endif
      binlog_pos = last_binlog_pos_;
      std::string last_manifest_file_dest = FileNameUtil::manifest_file_path(backup_tmp_dir_path_, last_manifest_file_num_);
      std::string last_manifest_file_src = FileNameUtil::manifest_file_path(db->GetName(), last_manifest_file_num_);
      DataDirFileChecker data_file_checker(first_manifest_file_num_, last_manifest_file_num_);
      WalDirFileChecker wal_file_checker(last_wal_file_num_);
      if (FAILED(link_files(db, (CurrentFileChecker *)nullptr, &data_file_checker, &wal_file_checker))) {
        SE_LOG(WARN, "Failed to link backup files", K(ret));
      } else if (FAILED(CopyFile(db->GetEnv(),
                                 last_manifest_file_src,
                                 last_manifest_file_dest,
                                 last_manifest_file_size_, // clang-format off
                                 false).code())) { // clang-format on
        // copy last manifest file
        SE_LOG(WARN, "Failed to copy last manifest file", K(ret));
      } else {
        SE_LOG(INFO,
               "Success to copy last manifest file and acquire snapshots",
               K(last_manifest_file_dest),
               K(last_manifest_file_num_),
               K(last_manifest_file_size_),
               K(last_wal_file_num_));
      }
    }
    db->EnableFileDeletions(false /**force enable*/);
  }

  if (FAILED(ret) && Status::kInitTwice != ret) {
    do_cleanup(db);
    reset();
  }

  return ret;
}

int BackupSnapshotImpl::record_incremental_extent_ids(DB *db)
{
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (FAILED(check_lock_status())) {
    SE_LOG(WARN, "Failed to check lock status", K(ret));
  } else if (FAILED(db->record_incremental_extent_ids(backup_tmp_dir_path_,
                                                      first_manifest_file_num_,
                                                      last_manifest_file_num_,
                                                      last_manifest_file_size_))) {
    SE_LOG(WARN, "Failed to record incremental extents", K(ret));
  } else {
    SE_LOG(INFO, "Success to record incremental extent-ids", K(ret));
  }
  if (FAILED(ret) && Status::kInitTwice != ret) {
    do_cleanup(db);
    reset();
  }

  return ret;
}

int BackupSnapshotImpl::release_old_backup_snapshot(DB *db, BackupSnapshotId backup_id) {
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (FAILED(backup_snapshot_map_.release_backup_snapshot(backup_id))) {
    SE_LOG(WARN, "Failed to release backup snapshot", K(ret), K(backup_id));
  } else {
    SE_LOG(INFO, "Success to release old backup snapshot", K(ret), K(backup_id));
  }
  return ret;
}

int BackupSnapshotImpl::release_current_backup_snapshot(DB *db)
{
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (FAILED(check_lock_status())) {
    SE_LOG(WARN, "Failed to check lock status", K(ret));
  } else if (FAILED(backup_snapshot_map_.release_backup_snapshot(cur_backup_id_))) {
    SE_LOG(WARN, "Failed to release backup snapshot", K(ret), K(cur_backup_id_));
  } else {
    SE_LOG(INFO, "Success to release current backup snapshot", K(ret), K(cur_backup_id_));
  }

  if (Status::kInitTwice != ret) {
    do_cleanup(db);
    reset();
  }
  return ret;
}

int BackupSnapshotImpl::list_backup_snapshots(std::vector<BackupSnapshotId> &backup_ids) {
  int ret = Status::kOk;
  BackupSnapshotId prev_backup_id = 0;
  BackupSnapshotId backup_id = 0;
  uint64_t auto_increment_id = 0;
  MetaSnapshotSet *backup_snapshot = nullptr;

  while (backup_snapshot_map_.get_next_backup_snapshot(prev_backup_id, backup_id, auto_increment_id, backup_snapshot)) {
    backup_ids.push_back(backup_id);
    prev_backup_id = backup_id;
  }
  return ret;
}

int BackupSnapshotImpl::create_tmp_dir(DB *db)
{
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (Status::kOk == (ret = db->GetEnv()->FileExists(backup_tmp_dir_path_).code())) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "Backup tmp dir exists, unexpected", K(ret), K(backup_tmp_dir_path_));
  } else if (ret != Status::kNotFound) {
    SE_LOG(WARN, "IO error when checking backup tmp dir", K(ret), K(backup_tmp_dir_path_));
  } else if (FAILED(db->GetEnv()->CreateDir(backup_tmp_dir_path_).code())) { // overwrite ret
    SE_LOG(WARN, "Failed to create tmp dir for backup", K(ret), K(backup_tmp_dir_path_));
  } else {
    SE_LOG(INFO, "Success to create tmp dir for backup", K(ret), K(backup_tmp_dir_path_));
  }
  return ret;
}

int BackupSnapshotImpl::cleanup_tmp_dir(DB *db)
{
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else {
    std::vector<std::string> all_files;
    if (FAILED(db->GetEnv()->GetChildren(backup_tmp_dir_path_, &all_files).code())) {
      if (Status::kNotFound != ret) {
        SE_LOG(WARN, "Failed to get all files in tmp dir", K(ret), K(backup_tmp_dir_path_));
      } else {
        // tmp dir not exist, overwrite ret
        ret = Status::kOk;
        SE_LOG(INFO, "Backup tmp dir is empty", K(backup_tmp_dir_path_));
      }
    } else {
      for (size_t i = 0; i < all_files.size(); i++) {
        if (all_files[i][0] != '.') {
          std::string file_path = backup_tmp_dir_path_ + "/" + all_files[i];
          if (FAILED(db->GetEnv()->DeleteFile(file_path).code())) { // overwrite ret
            SE_LOG(WARN, "Failed to delete file in backup tmp dir", K(ret), K(file_path));
          } else {
            SE_LOG(INFO, "Success to delete file in backup tmp dir", K(file_path));
          }
        }
      }
      if (FAILED(db->GetEnv()->DeleteDir(backup_tmp_dir_path_).code())) { // overwrite ret
        SE_LOG(WARN, "Failed to delete backup tmp dir", K(ret), K(backup_tmp_dir_path_));
      } else {
        SE_LOG(INFO, "Success to delete backup tmp dir", K(backup_tmp_dir_path_));
      }
    }
  }
  return ret;
}

// clean tmp files
int BackupSnapshotImpl::do_cleanup(DB *db)
{
  int ret = Status::kOk;
  if (IS_NULL(db)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "db is nullptr", K(ret));
  } else if (FAILED(StorageManager::cleanup_backup_snapshot(cur_meta_snapshots_))) {
    SE_LOG(WARN, "Failed to cleanup backup snapshot", K(ret));
  } else if (FAILED(cleanup_tmp_dir(db))) {
    SE_LOG(WARN, "Failed to cleanup tmp dir", K(ret));
  } else {
    SE_LOG(DEBUG, "Success to cleanup tmp dir", K(ret));
  }
  return ret;
}

} // namespace util
} // namespace smartengine
