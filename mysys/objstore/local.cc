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

#include "mysys/objstore/local.h"

#include <assert.h>
#include <sys/errno.h>
#include <algorithm>
#include <cerrno>
#include <chrono>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace objstore {

namespace fs = std::filesystem;

namespace {

Errors std_err_code_to_objstore_error(std::error_code err_code) {
  if (!err_code) {
    return Errors::SE_SUCCESS;
  }
  if (err_code.value() == ENOENT) {
    return Errors::SE_NO_SUCH_KEY;
  }
  return Errors::SE_IO_ERROR;
}

int get_obj_meta_from_file(fs::path path, ObjectMeta &meta, bool is_dir) {
  std::error_code errcode;

  fs::file_time_type ftime = fs::last_write_time(path, errcode);
  if (errcode.value() != 0) {
    return errcode.value();
  }
  int64_t epoch_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            ftime.time_since_epoch())
                            .count();

  std::uintmax_t fsize = -1;
  if (is_dir) {
    fsize = 0;
  } else {
    errcode.clear();
    fsize = fs::file_size(path, errcode);
    if (errcode.value() != 0) {
      return errcode.value();
    }
  }

  meta.last_modified = epoch_in_ms;
  meta.size = fsize;

  return 0;
}

}  // anonymous namespace

Status LocalObjectStore::create_bucket(const std::string_view &bucket) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(bucket)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid bucket");
  }

  int ret = mkdir_p(generate_path(bucket));
  Errors error_code = ret == 0 ? Errors::SE_SUCCESS : Errors::SE_IO_ERROR;
  return Status(error_code, ret, std::generic_category().message(ret));
}

Status LocalObjectStore::delete_bucket(const std::string_view &bucket) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(bucket)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid bucket");
  }

  int ret = rm_f(generate_path(bucket));
  Errors error_code = ret == 0 ? Errors::SE_SUCCESS : Errors::SE_IO_ERROR;
  return Status(error_code, ret, std::generic_category().message(ret));
}

Status LocalObjectStore::put_object_from_file(
    const std::string_view &bucket, const std::string_view &key,
    const std::string_view &data_file_path) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid key");
  }

  std::string key_path = generate_path(bucket, key);
  // key may contains '/', so if its parent directory does not exists, we
  // create for it.
  int ret = mkdir_p(fs::path(key_path).parent_path().native());
  assert(!ret);

  // open output file.
  std::ofstream output_file(key_path, std::ios::binary | std::ios::trunc);
  if (!output_file) {
    return Status(Errors::SE_INVALID, EINVAL, "Couldn't open file");
  }

  // open input file.
  std::ifstream input_file(std::string(data_file_path), std::ios::in | std::ios::binary);
  if (!input_file.is_open()) {
    std::error_code errcode(errno, std::generic_category());
    // if (ENOENT == errcode.value() && !fs::exists(generate_path(bucket))) {
    //   return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
    // }
    Errors error_code = std_err_code_to_objstore_error(errcode);
    return Status(error_code, errcode.value(), "Couldn't open file");
  }

  // read input file.
  input_file.seekg(0, std::ios::end);
  std::streamsize fileSize = input_file.tellg();
  input_file.seekg(0, std::ios::beg);

  std::string body;
  body.resize(fileSize);
  bool fail = !input_file.read(body.data(), body.size());
  input_file.close();
  if (fail) 
    return Status(Errors::SE_IO_ERROR, EINVAL, "read fail");
  // write output file.
  fail = !output_file.write(body.data(), body.size());
  output_file.close();
  return fail ? Status(Errors::SE_IO_ERROR, EINVAL, "write fail") : Status();
}

Status LocalObjectStore::get_object_to_file(
    const std::string_view &bucket, const std::string_view &key,
    const std::string_view &output_file_path) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid key");
  }

  std::string key_path = generate_path(bucket, key);

  // open output file.
  std::ofstream output_file(std::string(output_file_path), std::ios::binary | std::ios::trunc);
  if (!output_file) {
    return Status(Errors::SE_INVALID, EINVAL, "Couldn't open file");
  }

  // open input file.
  std::ifstream input_file(key_path, std::ios::in | std::ios::binary);
  if (!input_file.is_open()) {
    std::error_code errcode(errno, std::generic_category());
    // if (ENOENT == errcode.value() && !fs::exists(generate_path(bucket))) {
    //   return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
    // }
    Errors error_code = std_err_code_to_objstore_error(errcode);
    return Status(error_code, errcode.value(), "Couldn't open file");
  }

  // read input file.
  input_file.seekg(0, std::ios::end);
  std::streamsize fileSize = input_file.tellg();
  input_file.seekg(0, std::ios::beg);

  std::string body;
  body.resize(fileSize);
  bool fail = !input_file.read(body.data(), body.size());
  input_file.close();
  if (fail) 
    return Status(Errors::SE_IO_ERROR, EINVAL, "read fail");
  // write output file.
  fail = !output_file.write(body.data(), body.size());
  output_file.close();
  return fail ? Status(Errors::SE_IO_ERROR, EINVAL, "write fail") : Status();
}

Status LocalObjectStore::put_object(const std::string_view &bucket,
                                    const std::string_view &key,
                                    const std::string_view &data,
                                    bool forbid_overwrite) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid key");
  }

  std::string key_path = generate_path(bucket, key);

  // NOTICE:
  // when forbid_overwrite is true, we need to check whether the key exists
  // before write. if it exists, we return error.
  //
  // for local mode, there isn't a atomic operation to check and write, so we
  // just do check and write in two steps. since local mode is only used for
  // test, so it's ok.
  //
  // c++ 23 provide std::ios::noreplace flag for std::ofstream, which can
  // simplify this logic later if we support it.
  if (forbid_overwrite && fs::exists(key_path)) {
    return Status(Errors::SE_OBJECT_FORBID_OVERWRITE, EEXIST, "file exists");
  }

  if (key_path.back() == '/') {
    if (data.size() != 0) {
      return Status(Errors::SE_INVALID, EINVAL,
                    std::string("key is directory but have data:") + key_path);
    }
    int ret = mkdir_p(fs::path(key_path).native());
    if (ret != 0) {
      return Status(Errors::SE_IO_ERROR, ret,
                    std::generic_category().message(ret));
    }
    return Status();
  } else {
    // key may contains '/', so if its parent directory does not exists, we
    // create for it.
    int ret = mkdir_p(fs::path(key_path).parent_path().native());
    assert(!ret);
    std::ofstream output_file(key_path, std::ios::binary | std::ios::trunc);
    if (!output_file) {
      return Status(Errors::SE_INVALID, EINVAL, "Couldn't open file");
    }

    bool fail = !output_file.write(data.data(), data.size());
    output_file.close();
    return fail ? Status(Errors::SE_IO_ERROR, EINVAL, "write fail") : Status();
  }
}

Status LocalObjectStore::get_object(const std::string_view &bucket,
                                    const std::string_view &key,
                                    std::string &body) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid key");
  }

  std::string key_path = generate_path(bucket, key);
  if (key_path.back() == '/') {
    if (!fs::exists(key_path)) {
      // if (!fs::exists(generate_path(bucket))) {
      //   return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
      // } else {
      return Status(Errors::SE_NO_SUCH_KEY, ENOENT, "key not found");
      // }
    }
    body.clear();
    return Status();
  } else {
    std::ifstream input_file(key_path, std::ios::in | std::ios::binary);
    if (!input_file.is_open()) {
      std::error_code errcode(errno, std::generic_category());
      // if (ENOENT == errcode.value() && !fs::exists(generate_path(bucket))) {
      //   return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
      // }
      Errors error_code = std_err_code_to_objstore_error(errcode);
      return Status(error_code, errcode.value(), "Couldn't open file");
    }

    input_file.seekg(0, std::ios::end);
    std::streamsize fileSize = input_file.tellg();
    input_file.seekg(0, std::ios::beg);

    body.resize(fileSize);
    bool fail = !input_file.read(body.data(), body.size());
    input_file.close();
    return fail ? Status(Errors::SE_IO_ERROR, EIO, "read fail") : Status();
  }
}

Status LocalObjectStore::get_object(const std::string_view &bucket,
                                    const std::string_view &key, size_t off,
                                    size_t len, std::string &body) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid key");
  }

  std::string key_path = generate_path(bucket, key);
  if (key_path.back() == '/') {
    if (!fs::exists(key_path)) {
      // if (!fs::exists(generate_path(bucket))) {
      //   return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
      // } else {
      return Status(Errors::SE_NO_SUCH_KEY, ENOENT, "key not found");
      // }
    }
    body.clear();
    return Status();
  } else {
    std::ifstream input_file(key_path, std::ios::in | std::ios::binary);
    if (!input_file.is_open()) {
      std::error_code errcode(errno, std::generic_category());
      // if (ENOENT == errcode.value() && !fs::exists(generate_path(bucket))) {
      //   return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
      // }
      Errors error_code = std_err_code_to_objstore_error(errcode);
      return Status(error_code, errcode.value(), "Couldn't open file");
    }

    input_file.seekg(0, std::ios::end);
    std::streamsize fileSize = input_file.tellg();
    if (off >= static_cast<size_t>(fileSize)) {
      return Status(Errors::SE_INVALID, ERANGE, "offset out of range");
    }

    input_file.seekg(off);
    body.resize(len);
    bool fail = !input_file.read(body.data(), len);
    body.resize(input_file.gcount());
    if (input_file.eof() && input_file.gcount() > 0) {
      fail = false;
    }
    input_file.close();
    return fail ? Status(Errors::SE_IO_ERROR, EIO, "read fail") : Status();
  }
}

Status LocalObjectStore::get_object_meta(const std::string_view &bucket,
                                         const std::string_view &key,
                                         ObjectMeta &meta) {
  const std::lock_guard<std::mutex> _(mutex_);
  bool is_dir = false;

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL,
                  std::string("invalid key:") + key.data());
  }
  fs::path key_path = fs::path(generate_path(bucket, key));
  if (key.back() == '/' && fs::is_directory(key_path)) {
    is_dir = true;
  } else if (key.back() != '/' && fs::is_regular_file(key_path)) {
    // empty
  } else {
    return Status(Errors::SE_INVALID, EINVAL,
                  std::string("invalid key:") + key.data());
  }

  int ret = get_obj_meta_from_file(key_path, meta, is_dir);
  if (ret != 0) {
    return Status(Errors::SE_IO_ERROR, ret, "fail to get object meta");
  }

  std::string bucket_path = generate_path(bucket);
  // use lexically_relative() to remove the bucket prefix
  // example: entry: bucket/key_prefix_dir/key -> key_prefix_dir/key
  meta.key = key_path.lexically_relative(bucket_path).c_str();
  return Status();
}

Status process_local_obj_meta(ObjectMeta &meta,
                              const fs::directory_entry &entry,
                              std::vector<ObjectMeta> &objects,
                              std::string_view bucket_path,
                              const std::string_view &prefix, bool recursive) {
  bool is_dir = false;
  if (fs::is_directory(entry)) {
    is_dir = true;
    meta.key = entry.path().lexically_relative(bucket_path).c_str();
    if (meta.key.back() != '/') {
      meta.key.append("/");
    }
  } else if (fs::is_regular_file(entry)) {
    // use lexically_relative() to remove the bucket prefix
    // example: entry: bucket/key_prefix_dir/key -> key_prefix_dir/key
    meta.key = entry.path().lexically_relative(bucket_path).c_str();
  } else {
    return Status(Errors::SE_INVALID, EINVAL, "invalid file type");
  }

  if (meta.key.find(prefix) == 0) {
    if (recursive || is_first_level_sub_key(meta.key, prefix)) {
      int ret = get_obj_meta_from_file(entry.path(), meta, is_dir);
      if (ret != 0) {
        return Status(Errors::SE_IO_ERROR, ret, "fail to get object meta");
      }
      objects.push_back(meta);
    }
  }

  return Status();
}

Status LocalObjectStore::list_object(const std::string_view &bucket,
                                     const std::string_view &prefix
                                     [[maybe_unused]],
                                     bool recursive,
                                     std::string &start_after [[maybe_unused]],
                                     bool &finished,
                                     std::vector<ObjectMeta> &objects) {
  const std::lock_guard<std::mutex> _(mutex_);

  std::string bucket_path = generate_path(bucket);
  objects.clear();

  if (!fs::exists(bucket_path)) {
    return Status(Errors::SE_NO_SUCH_BUCKET, ENOENT, "bucket not found");
  }

  for (const auto &entry : fs::recursive_directory_iterator(bucket_path)) {
    ObjectMeta meta;
    Status s = process_local_obj_meta(meta, entry, objects, bucket_path,
                                      prefix, recursive);
    if (!s.is_succ()) {
      return s;
    }
  }

  // sort the objects by key in lexicographical order.
  std::sort(
      objects.begin(), objects.end(),
      [](const ObjectMeta &a, const ObjectMeta &b) { return a.key < b.key; });

  finished = true;
  return Status();
}

Status LocalObjectStore::delete_object(const std::string_view &bucket,
                                       const std::string_view &key) {
  const std::lock_guard<std::mutex> _(mutex_);

  if (!is_valid_key(key)) {
    return Status(Errors::SE_INVALID, EINVAL, "invalid key");
  }

  size_t len = key.size();
  if (key.back() == '/') {
    if (len > 1) {
      // should remove the last '/', otherwise the parent directory will be the
      // same as the key.
      len -= 1;
    } else {
      return Status(Errors::SE_INVALID, EINVAL,
                    std::string("invalid key:") + key.data());
    }
  }

  const std::string key_path_str = generate_path(bucket, key.substr(0, len));
  const std::string bucket_path_str = generate_path(bucket);

  fs::path key_path = fs::path(key_path_str);

  int ret = rm_f(key_path.c_str());
  if (ret != 0) {
    return Status(Errors::SE_IO_ERROR, ret,
                      std::generic_category().message(ret));
  }
  return Status();
}

Status LocalObjectStore::delete_objects(const std::string_view &bucket,
                                        const std::vector<std::string_view> &keys) {
  for (const std::string_view &key : keys) {
    Status s = delete_object(bucket, key);
    if (!s.is_succ()) {
      return s;
    }
  }
  return Status();
}

Status LocalObjectStore::delete_directory(const std::string_view &bucket, 
                                          const std::string_view &prefix) {
  std::string dir_prefix(prefix);
  if (!dir_prefix.empty() && dir_prefix.back() != '/') {
    dir_prefix.append("/");
  }
  std::string start_after;
  bool finished = false;
  std::vector<ObjectMeta> objects;
  while (!finished) {
    Status s =
        list_object(bucket, dir_prefix, true, start_after, finished, objects);
    if (!s.is_succ()) {
      return s;
    }
    if (!objects.empty()) {
      std::vector<std::string_view> object_keys;
      for (const ObjectMeta &object_meta : objects) {
        object_keys.emplace_back(object_meta.key);
      }
      s = delete_objects(bucket, object_keys);
      if (!s.is_succ()) {
        return s;
      }
      objects.clear();
    }
  }
  return Status();
}

std::string LocalObjectStore::generate_path(const std::string_view &bucket) {
  return std::string(basepath_) + "/" + std::string(bucket);
}

std::string LocalObjectStore::generate_path(const std::string_view &bucket,
                                            const std::string_view &key) {
  std::string key_buf(key);
  return std::string(basepath_) + "/" + std::string(bucket) + '/' +
         std::string(key_buf);
}

LocalObjectStore *create_local_objstore(const std::string_view region,
                                        const std::string_view *endpoint
                                        [[maybe_unused]],
                                        bool use_https [[maybe_unused]]) {
  int ret = mkdir_p(region);
  if (ret != 0) {
    return nullptr;
  }

  LocalObjectStore *lobs =
      new LocalObjectStore(region /* use region parameter as basepath */);

  if (lobs == nullptr) {
    rm_f(region);
  }

  return lobs;
}

LocalObjectStore *create_local_objstore(const std::string_view &access_key
                                        [[maybe_unused]],
                                        const std::string_view &secret_key
                                        [[maybe_unused]],
                                        const std::string_view region,
                                        const std::string_view *endpoint,
                                        bool use_https) {
  return create_local_objstore(region, endpoint, use_https);
}

void destroy_local_objstore(LocalObjectStore *local_objstore) {
  if (local_objstore) {
    delete local_objstore;
  }
  // keep the data there.
  return;
}

}  // namespace objstore
