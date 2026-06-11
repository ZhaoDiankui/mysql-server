//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "tools/ldb_cmd.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace smartengine {
namespace tools {

struct SliceHasher {
  size_t operator() (const common::Slice &s) const {
    return std::hash<std::string>()(std::string(s.data(), s.size()));
  }
};

struct SliceComparator {
  bool operator()(const common::Slice &lhs, const common::Slice &rhs) const {
    if (lhs.size() == rhs.size() &&
        !memcmp(lhs.data(), rhs.data(), lhs.size())) {
      return true;
    } else {
      return false;
    }
  }
};


class ManifestDumpCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "manifest_dump"; }

  ManifestDumpCommand(const std::vector<std::string>& params,
                      const std::map<std::string, std::string>& options,
                      const std::vector<std::string>& flags);

  static void Help(std::string& ret);
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }
  // for backup read the increment extents 
  //static int dump_manifest_file(std::string ckpname, bool verbose, bool hex, bool json,
  //                     std::unordered_map<common::Slice,
  //std::pair<common::Slice, int64_t>, SliceHasher, SliceComparator> *extents = nullptr);

 private:
  bool verbose_;
  bool json_;
  std::string path_;

  static const std::string ARG_VERBOSE;
  static const std::string ARG_JSON;
  static const std::string ARG_PATH;
};

class CheckpointDumpCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "checkpoint_dump"; }

  CheckpointDumpCommand(const std::vector<std::string>& params,
                      const std::map<std::string, std::string>& options,
                      const std::vector<std::string>& flags);

  static void Help(std::string& ret);
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }
  // for backup read the last version meta
  //TODO:yuanfeng
  //static int dump_checkpoint_file(std::string ckpname, bool verbose, bool hex, bool json,
  //                     std::unordered_map<common::Slice,
  //std::pair<common::Slice, int64_t>, SliceHasher, SliceComparator> *extents = nullptr);
  int dump_checkpoint_file();
 private:
  int dump_extent_space_mgr_checkpoint(util::RandomAccessFile *checkpoint_reader, CheckpointHeader *header);
  int dump_version_set_checkpoint(util::RandomAccessFile *checkpoint_reader, CheckpointHeader *header);
  int read_big_subtable(util::RandomAccessFile *checkpoint_reader, int64_t block_size, int64_t &file_offset);

 private:
  static const int64_t DEFAULT_BUFFER_SIZE = 2 * 1024 * 1024;
  static const int64_t DEFAULT_TO_STRING_SIZE = 16 * 1024 * 1024;
 private:
  bool verbose_;
  bool json_;
  std::string path_;

  static const std::string ARG_VERBOSE;
  static const std::string ARG_JSON;
  static const std::string ARG_PATH;
};

class ListColumnFamiliesCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "list_column_families"; }

  ListColumnFamiliesCommand(const std::vector<std::string>& params,
                            const std::map<std::string, std::string>& options,
                            const std::vector<std::string>& flags);

  static void Help(std::string& ret);
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }

 private:
  std::string dbname_;
};

class CreateColumnFamilyCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "create_column_family"; }

  CreateColumnFamilyCommand(const std::vector<std::string>& params,
                            const std::map<std::string, std::string>& options,
                            const std::vector<std::string>& flags);

  static void Help(std::string& ret);
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return false; }

 private:
  std::string new_cf_name_;
};

class WALDumperCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "dump_wal"; }

  WALDumperCommand(const std::vector<std::string>& params,
                   const std::map<std::string, std::string>& options,
                   const std::vector<std::string>& flags);

  virtual bool NoDBOpen() override { return true; }

  static void Help(std::string& ret);
  virtual void DoCommand() override;

 private:
  bool print_header_;
  std::string wal_file_;
  bool print_values_;

  static const std::string ARG_WAL_FILE;
  static const std::string ARG_PRINT_HEADER;
  static const std::string ARG_PRINT_VALUE;
};

class GetCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "get"; }

  GetCommand(const std::vector<std::string>& params,
             const std::map<std::string, std::string>& options,
             const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret);

 private:
  std::string key_;
};

class ApproxSizeCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "approxsize"; }

  ApproxSizeCommand(const std::vector<std::string>& params,
                    const std::map<std::string, std::string>& options,
                    const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret);

 private:
  std::string start_key_;
  std::string end_key_;
};

class BatchPutCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "batchput"; }

  BatchPutCommand(const std::vector<std::string>& params,
                  const std::map<std::string, std::string>& options,
                  const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret);

  virtual common::Options PrepareOptionsForOpenDB() override;

 private:
  /**
   * The key-values to be inserted.
   */
  std::vector<std::pair<std::string, std::string>> key_values_;
};

class DeleteCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "delete"; }

  DeleteCommand(const std::vector<std::string>& params,
                const std::map<std::string, std::string>& options,
                const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret);

 private:
  std::string key_;
};

class PutCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "put"; }

  PutCommand(const std::vector<std::string>& params,
             const std::map<std::string, std::string>& options,
             const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret);

  virtual common::Options PrepareOptionsForOpenDB() override;

 private:
  std::string key_;
  std::string value_;
};

/**
 * Command that starts up a REPL shell that allows
 * get/put/delete.
 */
class DBQuerierCommand : public tools::LDBCommand {
 public:
  static std::string Name() { return "query"; }

  DBQuerierCommand(const std::vector<std::string>& params,
                   const std::map<std::string, std::string>& options,
                   const std::vector<std::string>& flags);

  static void Help(std::string& ret);

  virtual void DoCommand() override;

 private:
  static const char* HELP_CMD;
  static const char* GET_CMD;
  static const char* PUT_CMD;
  static const char* DELETE_CMD;
};

}
}  // namespace smartengine
