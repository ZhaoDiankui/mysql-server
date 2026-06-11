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

#include <cstdint>
#include <string>
#include <string_view>
#include "objstore.h"

namespace objstore {

int tryBackupRecoveringLock(uint64_t auto_increment_id,
                            ObjectStore *objstore,
                            const std::string_view bucket_dir,
                            const std::string &cluster_objstore_id,
                            std::string &err_msg);

int tryBackupReleasingLock(uint64_t auto_increment_id,
                           ObjectStore *objstore,
                           const std::string_view bucket_dir,
                           const std::string &cluster_objstore_id,
                           std::string &err_msg);

int updateBackupStautsToReleasing(uint64_t auto_increment_id,
                                  ObjectStore *objstore,
                                  const std::string_view bucket_dir,
                                  const std::string &cluster_objstore_id,
                                  std::string &err_msg);

int removeBackupStatusLockFile(uint64_t auto_increment_id,
                               ObjectStore *objstore,
                               const std::string_view bucket_dir,
                               const std::string &cluster_objstore_id,
                               const std::string_view expected_status,
                               std::string &err_msg);

int removeObsoletedBackupStatusLockFiles(ObjectStore *objstore,
                                         const std::string_view bucket_dir,
                                         const std::string &cluster_objstore_id,
                                         uint64_t auto_increment_id_in_use,
                                         std::string &err_msg);

} // namespace objstore
