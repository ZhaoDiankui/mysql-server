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

#include "env/env.h"
#include "options/options.h"

#include <thread>
#include <execinfo.h>

namespace smartengine {
using namespace common;

namespace util {

Env::~Env() {}

uint64_t Env::GetThreadID() const {
  std::hash<std::thread::id> hasher;
  return hasher(std::this_thread::get_id());
}

Status Env::GetChildrenFileAttributes(const std::string& dir,
                                      std::vector<FileAttributes>* result) {
  assert(result != nullptr);
  std::vector<std::string> child_fnames;
  Status s = GetChildren(dir, &child_fnames);
  if (!s.ok()) {
    return s;
  }
  result->resize(child_fnames.size());
  size_t result_size = 0;
  for (size_t i = 0; i < child_fnames.size(); ++i) {
    const std::string path = dir + "/" + child_fnames[i];
    if (!(s = GetFileSize(path, &(*result)[result_size].size_bytes)).ok()) {
      if (FileExists(path).IsNotFound()) {
        // The file may have been deleted since we listed the directory
        continue;
      }
      return s;
    }
    (*result)[result_size].name = std::move(child_fnames[i]);
    result_size++;
  }
  result->resize(result_size);
  return Status::OK();
}

SequentialFile::~SequentialFile() {}

RandomAccessFile::~RandomAccessFile() {}

WritableFile::~WritableFile() {}

FileLock::~FileLock() {}

Status WriteStringToFile(Env* env, const Slice& data, const std::string& fname,
                         bool should_sync) {
  unique_ptr<WritableFile, memory::ptr_destruct_delete<WritableFile>> file_ptr;
  WritableFile *file = nullptr;
  EnvOptions soptions;
  Status s = env->NewWritableFile(fname, file, soptions);
  file_ptr.reset(file);
  if (!s.ok()) {
    return s;
  }
  s = file->Append(data);
  if (s.ok() && should_sync) {
    s = file->Sync();
  }
  if (!s.ok()) {
    env->DeleteFile(fname);
  }
  return s;
}

Status ReadFileToString(Env* env, const std::string& fname, std::string* data) {
  EnvOptions soptions;
  data->clear();
  unique_ptr<SequentialFile, memory::ptr_destruct_delete<SequentialFile>> file_ptr;
  SequentialFile *file = nullptr;
  Status s = env->NewSequentialFile(fname, file, soptions);
  file_ptr.reset(file);
  if (!s.ok()) {
    return s;
  }
  static const int kBufferSize = 8192;
  char *space = (char *)memory::base_malloc(kBufferSize);
  while (true) {
    Slice fragment;
    s = file->Read(kBufferSize, &fragment, space);
    if (!s.ok()) {
      break;
    }
    data->append(fragment.data(), fragment.size());
    if (fragment.empty()) {
      break;
    }
  }
  memory::base_free(space);
  return s;
}

EnvWrapper::~EnvWrapper() {}

namespace {  // anonymous namespace
//TODO: Zhao Dongsheng, this function is used by EnvOptions() only, can optimize.
void AssignEnvOptions(EnvOptions* env_options, const DBOptions& options) {
  env_options->use_direct_reads = options.use_direct_reads;
  env_options->bytes_per_sync = options.bytes_per_sync;
  env_options->rate_limiter = options.rate_limiter.get();
  env_options->writable_file_max_buffer_size =
      options.writable_file_max_buffer_size;
  env_options->concurrent_writable_file_buffer_num =
      options.concurrent_writable_file_buffer_num;
  env_options->concurrent_writable_file_single_buffer_size =
      options.concurrent_writable_file_single_buffer_size;
  env_options->concurrent_writable_file_buffer_switch_limit =
      options.concurrent_writable_file_buffer_switch_limit;
}
}

EnvOptions Env::OptimizeForLogWrite(const EnvOptions& env_options,
                                    const DBOptions& db_options) const {
  EnvOptions optimized_env_options(env_options);
  optimized_env_options.bytes_per_sync = db_options.wal_bytes_per_sync;
  return optimized_env_options;
}

EnvOptions Env::OptimizeForManifestWrite(const EnvOptions& env_options) const {
  return env_options;
}

EnvOptions::EnvOptions(const DBOptions& options) {
  AssignEnvOptions(this, options);
}

EnvOptions::EnvOptions() {
  DBOptions options;
  AssignEnvOptions(this, options);
}

}  // namespace util
}  // namespace smartengine
