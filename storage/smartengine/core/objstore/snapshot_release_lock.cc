/*
   Copyright (c) 2024, ApeCloud Inc Holding Limited.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include "objstore/snapshot_release_lock.h"

#include "objstore/objstore_layout.h"

namespace objstore {

namespace {

const char *snapshot_releasing_status = "releasing";
const char *snapshot_recovering_status = "recovering";
const char *snapshot_status_lock_prefix = "snapshot_status_";

int tryObjectStoreFileLock(ObjectStore *objstore,
                           const std::string_view bucket_dir,
                           std::string &err_msg,
                           std::string &key,
                           std::string &data,
                           const char *backup_snapshot_status)
{
  int ret = 0;
  if (objstore == nullptr) {
    err_msg = "object store is not initialized";
    ret = Errors::SE_INVALID;
  } else {
    bool forbid_overwrite = true;
    Status status = objstore->put_object(bucket_dir, key, data, forbid_overwrite);
    if (status.error_code() == Errors::SE_OBJECT_FORBID_OVERWRITE) {
      // the lock file already exists
      std::string current_status;
      status = objstore->get_object(bucket_dir, key, current_status);
      if (status.error_code() == Errors::SE_NO_SUCH_KEY) {
        err_msg = key + ": lock file may be deleted by another node";
        ret = Errors::SE_OHTER_DATA_NODE_MAYBE_RUNNING;
      } else if (!status.is_succ()) {
        err_msg = key + ": get lock file failed: " + status.error_message().data();
        ret = status.error_code();
      } else {
        if (current_status == backup_snapshot_status) {
          // lock file exists and with the same status, this may happen
          // when a data node was killed/crashed after the lock file
          // written. since we don't support muliple data nodes at now, we
          // treat this as an normal case.
        } else {
          err_msg = key + ": lock file exists but in " + current_status + " status";
          ret = Errors::SE_OHTER_DATA_NODE_MAYBE_RUNNING;
        }
      }
    } else if (!status.is_succ()) {
      err_msg = key + ": put lock file failed: " + status.error_message().data();
      ret = status.error_code();
    } else {
      // lock file is created successfully
    }
  }
  return ret;
}

int tryBackupStatusLock(uint64_t auto_increment_id,
                        ObjectStore *objstore,
                        const std::string_view bucket_dir,
                        const std::string &cluster_objstore_id,
                        std::string &err_msg,
                        const char *backup_snapshot_status)
{
  std::string key = smartengine::util::make_lock_prefix(cluster_objstore_id) + snapshot_status_lock_prefix +
                    std::to_string(auto_increment_id);
  std::string data = backup_snapshot_status;
  return tryObjectStoreFileLock(objstore, bucket_dir, err_msg, key, data, backup_snapshot_status);
}

} // namespace

int tryBackupRecoveringLock(uint64_t auto_increment_id,
                            ObjectStore *objstore,
                            const std::string_view bucket_dir,
                            const std::string &cluster_objstore_id,
                            std::string &err_msg)
{
  return tryBackupStatusLock(auto_increment_id,
                             objstore,
                             bucket_dir,
                             cluster_objstore_id,
                             err_msg,
                             snapshot_recovering_status);
}

// backup releasing lock is for the case that mulitple instances of
// smartengine are running, and one instance is doing recovering based on
// a snapshot X, and the other instance is doing release snapshot X. we
// use this lock to prevent a snapshot being released when it is being
// used.
int tryBackupReleasingLock(uint64_t auto_increment_id,
                           ObjectStore *objstore,
                           const std::string_view bucket_dir,
                           const std::string &cluster_objstore_id,
                           std::string &err_msg)
{
  return tryBackupStatusLock(auto_increment_id,
                             objstore,
                             bucket_dir,
                             cluster_objstore_id,
                             err_msg,
                             snapshot_releasing_status);
}

int updateBackupStautsToReleasing(uint64_t auto_increment_id,
                                  ObjectStore *objstore,
                                  const std::string_view bucket_dir,
                                  const std::string &cluster_objstore_id,
                                  std::string &err_msg)
{
  Status status;
  int ret = 0;
  if (objstore == nullptr) {
    err_msg = "object store is not initialized";
    return Errors::SE_INVALID;
  }
  std::string key = smartengine::util::make_lock_prefix(cluster_objstore_id) + snapshot_status_lock_prefix +
                    std::to_string(auto_increment_id);
  std::string value;
  status = objstore->get_object(bucket_dir, key, value);
  if (!status.is_succ()) {
    err_msg = status.error_message();
    ret = status.error_code();
    // } else if (value !=  expected_status) {
  } else if (value != objstore::snapshot_recovering_status) {
    err_msg = std::string("snapshot is not in ") + objstore::snapshot_recovering_status + " status, actual status is " +
              value;
    ret = 1;
  } else {
    status = objstore->put_object(bucket_dir, key, objstore::snapshot_releasing_status, false);
    if (!status.is_succ()) {
      err_msg = status.error_message();
      ret = status.error_code();
    }
  }
  return ret;
}

int removeBackupStatusLockFile(uint64_t auto_increment_id,
                               ObjectStore *objstore,
                               const std::string_view bucket_dir,
                               const std::string &cluster_objstore_id,
                               const std::string_view expected_status,
                               std::string &err_msg)
{
  Status status;
  int ret = 0;
  if (objstore == nullptr) {
    err_msg = "object store is not initialized";
    ret = Errors::SE_INVALID;
    return ret;
  }

  std::string key = smartengine::util::make_lock_prefix(cluster_objstore_id) + snapshot_status_lock_prefix +
                    std::to_string(auto_increment_id);
  std::string value;
  status = objstore->get_object(bucket_dir, key, value);
  if (!status.is_succ()) {
    err_msg = status.error_message();
    ret = status.error_code();
  } else if (value != expected_status) {
    err_msg = std::string("snapshot is not in ") + expected_status.data() + " status, actual status is " + value;
    ret = 1;
  } else {
    status = objstore->delete_object(bucket_dir, key);
    if (!status.is_succ()) {
      err_msg = status.error_message();
      ret = status.error_code();
    }
  }
  return ret;
}

int removeObsoletedBackupStatusLockFiles(ObjectStore *objstore,
                                         const std::string_view bucket_dir,
                                         const std::string &cluster_objstore_id,
                                         uint64_t auto_increment_id_for_recover,
                                         std::string &err_msg)
{
  Status status;
  int ret = 0;
  if (objstore == nullptr) {
    ret = 1;
    err_msg = "object store is not initialized";
    return ret;
  }

  std::string prefix = smartengine::util::make_lock_prefix(cluster_objstore_id) + snapshot_status_lock_prefix;
  std::string lock_file_to_exclude = prefix + std::to_string(auto_increment_id_for_recover);
  std::string start_after;
  bool finished = false;
  do {
    std::vector<ObjectMeta> objects;
    status = objstore->list_object(bucket_dir, prefix, false, start_after, finished, objects);
    if (!status.is_succ()) {
      ret = status.error_code();
      err_msg = status.error_message();
    } else {
      std::string value;
      std::vector<std::string_view> object_keys;
      for (const auto &object : objects) {
        if (object.key == lock_file_to_exclude) {
          continue;
        }
        object_keys.emplace_back(object.key);
      }

      status = objstore->delete_objects(bucket_dir, object_keys);
      if (!status.is_succ()) {
        ret = status.error_code();
        err_msg = status.error_message();
      }
    }
  } while (!finished && ret == 0);

  return ret;
}

} // namespace objstore