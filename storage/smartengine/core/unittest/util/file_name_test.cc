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

#include "util/file_name.h"
#include "testharness.h"
#include "util/se_constants.h"

namespace smartengine
{
using namespace common;
namespace util
{
TEST(FileNameUtil, with_number)
{
  struct FileInfo {
    std::string (*name_func)(int64_t);
    std::string (*path_func)(const std::string &, int64_t);
    std::string file_name = nullptr;
    std::string file_path = nullptr;
    std::string dir = nullptr;
    FileType file_type = kInvalidFileType;
    int64_t file_number = 0;
  };
  FileInfo file_infos[] = {
      {&FileNameUtil::wal_file_name, &FileNameUtil::wal_file_path, "0.wal", "ppl/0.wal", "ppl", kWalFile, 0},
      {&FileNameUtil::wal_file_name, &FileNameUtil::wal_file_path, "1000000.wal", "ppl/1000000.wal", "ppl", kWalFile, 1000000},
      {&FileNameUtil::wal_file_name, &FileNameUtil::wal_file_path, "9223372036854775807.wal", "ppl/9223372036854775807.wal", "ppl", kDataFile, 9223372036854775807},
      {&FileNameUtil::data_file_name, &FileNameUtil::data_file_path, "0.sst", "ppl/0.sst", "ppl", kDataFile, 0},
      {&FileNameUtil::data_file_name, &FileNameUtil::data_file_path, "1000000.sst", "ppl/1000000.sst", "ppl", kDataFile, 1000000},
      {&FileNameUtil::data_file_name, &FileNameUtil::data_file_path, "9223372036854775807.sst", "ppl/9223372036854775807.sst", "ppl", kDataFile, 9223372036854775807},
      {&FileNameUtil::manifest_file_name, &FileNameUtil::manifest_file_path, "0.manifest", "ppl/0.manifest", "ppl", kManifestFile, 0},
      {&FileNameUtil::manifest_file_name, &FileNameUtil::manifest_file_path, "1000000.manifest", "ppl/1000000.manifest", "ppl", kManifestFile, 1000000},
      {&FileNameUtil::manifest_file_name, &FileNameUtil::manifest_file_path, "9223372036854775807.manifest", "ppl/9223372036854775807.manifest", "ppl", kManifestFile, 9223372036854775807},
      {&FileNameUtil::checkpoint_file_name, &FileNameUtil::checkpoint_file_path, "0.checkpoint", "ppl/0.checkpoint", "ppl", kCheckpointFile, 0},
      {&FileNameUtil::checkpoint_file_name, &FileNameUtil::checkpoint_file_path, "1000000.checkpoint", "ppl/1000000.checkpoint", "ppl", kCheckpointFile, 1000000},
      {&FileNameUtil::checkpoint_file_name, &FileNameUtil::checkpoint_file_path, "9223372036854775807.checkpoint", "ppl/9223372036854775807.checkpoint", "ppl", kCheckpointFile, 9223372036854775807},
      {&FileNameUtil::temp_file_name, &FileNameUtil::temp_file_path, "0.tmp", "ppl/0.tmp", "ppl", kTempFile, 0},
      {&FileNameUtil::temp_file_name, &FileNameUtil::temp_file_path, "1000000.tmp", "ppl/1000000.tmp", "ppl", kTempFile, 1000000},
      {&FileNameUtil::temp_file_name, &FileNameUtil::temp_file_path, "9223372036854775807.tmp", "ppl/9223372036854775807.tmp", "ppl", kTempFile, 9223372036854775807},
  };

  for (uint64_t i = 0; i < sizeof(file_infos) / sizeof(file_infos[0]); ++i) {
    std::string file_name;
    std::string file_path;

    if (IS_NOTNULL(file_infos[i].name_func)) {
      file_name = file_infos[i].name_func(file_infos[i].file_number);
      ASSERT_EQ(file_infos[i].file_name, file_name);
    }

    if (IS_NOTNULL(file_infos[i].path_func)) {
      file_path = file_infos[i].path_func(file_infos[i].dir, file_infos[i].file_number);
      ASSERT_EQ(file_infos[i].file_path, file_path);
    }
  }
}

TEST(FileNameUtil, without_number)
{
  struct FileInfo {
    std::string (*path_func)(const std::string &);
    std::string file_path;
    std::string dir;
  };
  FileInfo file_infos[] = {
      {&FileNameUtil::current_file_path, "ppl/CURRENT", "ppl"},
      {&FileNameUtil::lock_file_path, "ppl/LOCK", "ppl"},
  };

  for (uint64_t i = 0; i < sizeof(file_infos) / sizeof(file_infos[0]); ++i) {
    std::string file_path = file_infos[i].path_func(file_infos[i].dir);
    ASSERT_EQ(file_path, file_infos[i].file_path);
  }
}

TEST(FileNameUtil, parse_file_name)
{
  struct FileInfo {
    std::string file_name;
    FileType file_type = kInvalidFileType;
    int64_t file_number = 0;
    int ret = Status::kOk;
  };

  FileInfo file_infos[] = {
      {"", kInvalidFileType, 0, Status::kInvalidArgument},
      {".", kInvalidFileType, 0, Status::kNotSupported},
      {"..", kInvalidFileType, 0, Status::kNotSupported},
      {"ppl", kInvalidFileType, 0, Status::kErrorUnexpected},
      {"123", kInvalidFileType, 0, Status::kErrorUnexpected},
      {"123.", kInvalidFileType, 0, Status::kErrorUnexpected},
      {"123-wal", kInvalidFileType, 0, Status::kErrorUnexpected},
      {"123.wa", kInvalidFileType, 123, Status::kNotSupported},
      {"CURRENT", kCurrentFile, 0, Status::kOk},
      {"LOCK", kLockFile, 0, Status::kOk},
      {"0.wal", kWalFile, 0, Status::kOk},
      {"1000000.wal", kWalFile, 1000000, Status::kOk},
      {"9223372036854775807.wal", kWalFile, 9223372036854775807, Status::kOk},
      {"0.sst", kDataFile, 0, Status::kOk},
      {"1000000.sst", kDataFile, 1000000, Status::kOk},
      {"9223372036854775807.sst", kDataFile, 9223372036854775807, Status::kOk},
      {"0.manifest", kManifestFile, 0, Status::kOk},
      {"1000000.manifest", kManifestFile, 1000000, Status::kOk},
      {"9223372036854775807.manifest", kManifestFile, 9223372036854775807, Status::kOk},
      {"0.checkpoint", kCheckpointFile, 0, Status::kOk},
      {"1000000.checkpoint", kCheckpointFile, 1000000, Status::kOk},
      {"9223372036854775807.checkpoint", kCheckpointFile, 9223372036854775807, Status::kOk},
      {"0.tmp", kTempFile, 0, Status::kOk},
      {"1000000.tmp", kTempFile, 1000000, Status::kOk},
      {"9223372036854775807.tmp", kTempFile, 9223372036854775807, Status::kOk},
  };

  for (uint64_t i = 0; i < sizeof(file_infos) / sizeof(file_infos[i]); ++i) {
    int ret = Status::kOk;
    FileType file_type = kInvalidFileType;
    int64_t file_number = 0;
    ret = FileNameUtil::parse_file_name(file_infos[i].file_name, file_number, file_type);
    ASSERT_EQ(file_infos[i].ret, ret);
    ASSERT_EQ(file_infos[i].file_type, file_type);
    ASSERT_EQ(file_infos[i].file_number, file_number);
  }
}

}  // namespace util
}  // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}