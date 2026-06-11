//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <string>
#include <vector>


namespace smartengine {
namespace db {

class MemTable;

struct JobContext {
  inline bool HaveSomethingToDelete() const {
    return full_scan_candidate_files.size() ||
           log_delete_files.size() ||
           new_superversion != nullptr || superversions_to_free.size() > 0 ||
           memtables_to_free.size() > 0;
  }

  // Structure to store information for candidate files to delete.
  struct CandidateFileInfo {
    std::string file_name;
    uint32_t path_id;
    CandidateFileInfo(std::string name, uint32_t path)
        : file_name(std::move(name)), path_id(path) {}
    bool operator==(const CandidateFileInfo& other) const {
      return file_name == other.file_name && path_id == other.path_id;
    }
  };

  // Unique job id
  int job_id;

  // a list of all files that we'll consider deleting
  // (every once in a while this is filled up with all files
  // in the DB directory)
  // (filled only if we're doing full scan)
  std::vector<CandidateFileInfo> full_scan_candidate_files;

  // a list of log files that we need to delete
  std::vector<uint64_t> log_delete_files;

  // a list of memtables to be free
  util::autovector<MemTable*> memtables_to_free;

  util::autovector<SuperVersion*> superversions_to_free;

  SuperVersion* new_superversion;  // if nullptr no new superversion

  int64_t log_number = 0;
  int64_t prev_log_number = 0;

  uint64_t prev_total_log_size = 0;
  size_t num_alive_log_files = 0;
  uint64_t size_log_to_delete = 0;

  //TODO(Zhao Dongsheng) the follow member variables should not be here.
  TaskType task_type_;
  int64_t output_level_;

  explicit JobContext(int _job_id, bool create_superversion = false)
  {
    job_id = _job_id;
    log_number = 0;
    prev_log_number = 0;
    new_superversion = create_superversion ? MOD_NEW_OBJECT(memory::ModId::kSuperVersion, SuperVersion) : nullptr;
    task_type_ = TaskType::FLUSH_TASK;
    output_level_ = 0;
  }

  // For non-empty JobContext Clean() has to be called at least once before
  // before destruction (see asserts in ~JobContext()). Should be called with
  // unlocked DB mutex. Destructor doesn't call Clean() to avoid accidentally
  // doing potentially slow Clean() with locked DB mutex.
  void Clean() {
    // free pending memtables
    for (auto m : memtables_to_free) {
      MOD_DELETE_OBJECT(MemTable, m);
    }
    // free superversions
    for (auto s : superversions_to_free) {
      MOD_DELETE_OBJECT(SuperVersion, s);
    }
    // if new_superversion was not used, it will be non-nullptr and needs
    // to be freed here
    MOD_DELETE_OBJECT(SuperVersion, new_superversion);

    memtables_to_free.clear();
    superversions_to_free.clear();
    new_superversion = nullptr;
  }

  ~JobContext() {
    assert(memtables_to_free.size() == 0);
    assert(superversions_to_free.size() == 0);
    assert(new_superversion == nullptr);
  }
};
}
}  // namespace smartengine
