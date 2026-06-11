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

#include <unistd.h>
#include <string>
#include <sys/stat.h>
#include <sys/statfs.h>
#include "env/env.h"

// For non linux platform, the following macros are used only as place
// holder.
#if !(defined OS_LINUX) && !(defined CYGWIN)
#define POSIX_FADV_NORMAL 0     /* [MC1] no further special treatment */
#define POSIX_FADV_RANDOM 1     /* [MC1] expect random page refs */
#define POSIX_FADV_SEQUENTIAL 2 /* [MC1] expect sequential page refs */
#define POSIX_FADV_WILLNEED 3   /* [MC1] will need these pages */
#define POSIX_FADV_DONTNEED 4   /* [MC1] dont need these pages */
#endif

namespace smartengine
{
namespace util
{
struct AIOInfo;

common::Status IOError(const std::string& context, int err_number) __attribute__((unused));

class PosixHelper {
 public:
  static size_t GetUniqueIdFromFile(int fd, char* id, size_t max_size);
};

class PosixSequentialFile : public SequentialFile {
 private:
  std::string filename_;
  FILE* file_;
  int fd_;
  bool use_direct_io_;
  size_t logical_sector_size_;

 public:
  PosixSequentialFile(const std::string& fname, FILE* file, int fd,
                      const EnvOptions& options);
  virtual ~PosixSequentialFile() override;

  virtual common::Status Read(size_t n, common::Slice* result,
                              char* scratch) override;
  virtual common::Status PositionedRead(uint64_t offset, size_t n,
                                        common::Slice* result,
                                        char* scratch) override;
  virtual common::Status Skip(uint64_t n) override;
  virtual common::Status InvalidateCache(size_t offset, size_t length) override;
  virtual bool use_direct_io() const override { return use_direct_io_; }
  virtual size_t GetRequiredBufferAlignment() const override {
    return logical_sector_size_;
  }
  virtual int fill_aio_info(uint64_t offset, size_t size, AIOInfo &aio_info) const override;
  virtual int file_size() const {
    struct stat buf;
    fstat(fd_, &buf);
    return buf.st_size;
  }
};

class PosixRandomAccessFile : public RandomAccessFile {
 protected:
  std::string filename_;
  int fd_;
  bool use_direct_io_;
  size_t logical_sector_size_;

 public:
  PosixRandomAccessFile(const std::string& fname, int fd,
                        const EnvOptions& options);
  virtual ~PosixRandomAccessFile() override;

  virtual common::Status Read(uint64_t offset, size_t n, common::Slice* result,
                              char* scratch) const override;
#if defined(OS_LINUX) || defined(OS_MACOSX)
  virtual size_t GetUniqueId(char* id, size_t max_size) const override;
#endif
  virtual void Hint(AccessPattern pattern) override;
  virtual common::Status InvalidateCache(size_t offset, size_t length) override;
  virtual bool use_direct_io() const override { return use_direct_io_; }
  virtual size_t GetRequiredBufferAlignment() const override {
    return logical_sector_size_;
  }
};

class PosixWritableFile : public WritableFile {
 protected:
  const std::string filename_;
  const bool use_direct_io_;
  int fd_;
  uint64_t filesize_;
  size_t logical_sector_size_;
#ifdef ROCKSDB_FALLOCATE_PRESENT
  bool allow_fallocate_;
  bool fallocate_with_keep_size_;
#endif

 public:
  explicit PosixWritableFile(const std::string& fname, int fd,
                             const EnvOptions& options);
  virtual ~PosixWritableFile() override;

  // Need to implement this so the file is truncated correctly
  // with direct I/O
  virtual common::Status Truncate(uint64_t size) override;
  virtual common::Status Close() override;
  virtual common::Status Append(const common::Slice& data) override;
  virtual common::Status PositionedAppend(const common::Slice& data,
                                          uint64_t offset) override;
  virtual common::Status Flush() override;
  virtual common::Status Sync() override;
  virtual common::Status Fsync() override;
  virtual bool IsSyncThreadSafe() const override;
  virtual bool use_direct_io() const override { return use_direct_io_; }
  virtual uint64_t GetFileSize() override;
  virtual common::Status InvalidateCache(size_t offset, size_t length) override;
  virtual size_t GetRequiredBufferAlignment() const override {
    return logical_sector_size_;
  }
#ifdef ROCKSDB_FALLOCATE_PRESENT
  virtual common::Status Allocate(uint64_t offset, uint64_t len) override;
#endif
#ifdef OS_LINUX
  virtual common::Status RangeSync(uint64_t offset, uint64_t nbytes) override;
  virtual size_t GetUniqueId(char* id, size_t max_size) const override;
#endif
};

class PosixRandomRWFile : public RandomRWFile {
 public:
  explicit PosixRandomRWFile(const std::string& fname, int fd,
                             const EnvOptions& options);
  virtual ~PosixRandomRWFile() override;

  virtual common::Status Write(uint64_t offset,
                               const common::Slice& data) override;

  virtual common::Status Read(uint64_t offset, size_t n, common::Slice* result,
                              char* scratch) const override;
  virtual int fallocate(int mode, int64_t offset, int64_t length) override;
  virtual int ftruncate(int64_t length) override;
  virtual common::Status Flush() override;
  virtual common::Status Sync() override;
  virtual common::Status Fsync() override;
  virtual common::Status Close() override;
  virtual int get_fd() override { return fd_; }

 private:
  const std::string filename_;
  int fd_;
};

class PosixDirectory : public Directory {
 public:
  explicit PosixDirectory(int fd) : fd_(fd) {}
  virtual ~PosixDirectory() override;
  virtual common::Status Fsync() override;

 private:
  int fd_;
};

}  // namespace util
}  // namespace smartengine
