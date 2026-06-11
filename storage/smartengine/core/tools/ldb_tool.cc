//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#include "tools/ldb_tool.h"
#include "tools/ldb_cmd_impl.h"
#include "tools/ldb_cmd.h"

namespace smartengine {
using namespace common;
using namespace util;
using namespace db;
using namespace table;
using namespace cache;

namespace tools {

LDBOptions::LDBOptions() {}

void LDBCommandRunner::PrintHelp(const LDBOptions& ldb_options,
                                 const char* exec_name) {
  std::string ret;

  ret.append(ldb_options.print_help_header);
  ret.append("\n\n");
  ret.append("commands MUST specify --" + LDBCommand::ARG_DB +
             "=<full_path_to_db_directory> when necessary\n");
  ret.append("\n");
  ret.append(
      "The following optional parameters control if keys/values are "
      "input/output as hex or as plain strings:\n");
  ret.append("  --" + LDBCommand::ARG_KEY_HEX +
             " : Keys are input/output as hex\n");
  ret.append("  --" + LDBCommand::ARG_VALUE_HEX +
             " : Values are input/output as hex\n");
  ret.append("  --" + LDBCommand::ARG_HEX +
             " : Both keys and values are input/output as hex\n");
  ret.append("\n");

  ret.append(
      "The following optional parameters control the database "
      "internals:\n");
  ret.append(
      "  --" + LDBCommand::ARG_CF_NAME +
      "=<string> : name of the sub table to operate on. default: default "
      "sub table\n");
  ret.append("  --" + LDBCommand::ARG_TTL +
             " with 'put','get','scan','dump','query','batchput'"
             " : DB supports ttl and value is internally timestamp-suffixed\n");
  ret.append("  --" + LDBCommand::ARG_BLOOM_BITS + "=<int,e.g.:14>\n");
  ret.append("  --" + LDBCommand::ARG_COMPRESSION_MAX_DICT_BYTES +
             "=<int,e.g.:16384>\n");
  ret.append("  --" + LDBCommand::ARG_BLOCK_SIZE + "=<block_size_in_bytes>\n");
  ret.append("  --" + LDBCommand::ARG_AUTO_COMPACTION + "=<true|false>\n");
  ret.append("  --" + LDBCommand::ARG_DB_WRITE_BUFFER_SIZE +
             "=<int,e.g.:16777216>\n");
  ret.append("  --" + LDBCommand::ARG_WRITE_BUFFER_SIZE +
             "=<int,e.g.:4194304>\n");

  ret.append("\n\n");
  ret.append("Data Access Commands:\n");
  PutCommand::Help(ret);
  GetCommand::Help(ret);
  BatchPutCommand::Help(ret);
  DeleteCommand::Help(ret);
  DBQuerierCommand::Help(ret);
  ApproxSizeCommand::Help(ret);

  ret.append("\n\n");
  ret.append("Admin Commands:\n");
  WALDumperCommand::Help(ret);
  ManifestDumpCommand::Help(ret);
  CheckpointDumpCommand::Help(ret);
  ListColumnFamiliesCommand::Help(ret);

  fprintf(stderr, "%s\n", ret.c_str());
}

void LDBCommandRunner::RunCommand(
    int argc, char** argv, Options options, const LDBOptions& ldb_options,
    const std::vector<ColumnFamilyDescriptor>* column_families) {
  if (argc <= 2) {
    PrintHelp(ldb_options, argv[0]);
    exit(1);
  }

  LDBCommand* cmdObj = LDBCommand::InitFromCmdLineArgs(
      argc, argv, options, ldb_options, column_families);
  if (cmdObj == nullptr) {
    fprintf(stderr, "Unknown command\n");
    PrintHelp(ldb_options, argv[0]);
    exit(1);
  }

  if (!cmdObj->ValidateCmdLineOptions()) {
    exit(1);
  }

  cmdObj->Run();
  LDBCommandExecuteResult ret = cmdObj->GetExecuteState();
  fprintf(stderr, "%s\n", ret.ToString().c_str());
  delete cmdObj;

  exit(ret.IsFailed());
}

void LDBTool::Run(int argc, char** argv, Options options,
                  const LDBOptions& ldb_options,
                  const std::vector<ColumnFamilyDescriptor>* column_families) {
  LDBCommandRunner::RunCommand(argc, argv, options, ldb_options,
                               column_families);
}
}  // namespace tools
}  // namespace smartengine