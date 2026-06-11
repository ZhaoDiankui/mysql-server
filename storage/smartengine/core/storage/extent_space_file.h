/*
 * Copyright (c) 2023, ApeCloud Inc Holding Limited
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

#ifndef SMARTENGINE_INCLUDE_EXTENT_SPACE_FILE_H_
#define SMARTENGINE_INCLUDE_EXTENT_SPACE_FILE_H_

#include <map>
#include <unordered_map>

#include "storage/extent_space.h"

namespace smartengine {
namespace storage {

class FileExtentSpace : public ExtentSpace {
 public:
  FileExtentSpace(util::Env *env, const util::EnvOptions &env_options);
  virtual ~FileExtentSpace() override;

  void destroy() override;

  // ddl relative function
  int create(const CreateExtentSpaceArgs &arg) override;
  int remove() override;

  // extent relative function
  int allocate(const std::string prefix, ExtentIOInfo &io_info) override;
  int recycle(const std::string prefix, const ExtentId extent_id) override;
  // mark the extent used, only used during recovery
  int reference_if_need(const std::string prefix,
                        const ExtentId extent_id,
                        ExtentIOInfo &io_info,
                        bool &existed) override;

  // shrink relative function
  int get_shrink_info_if_need(const ShrinkCondition &shrink_condition,
                              bool &need_shrink,
                              ShrinkInfo &shrink_info) override;
  int move_extens_to_front(
      const int64_t move_extent_count,
      std::unordered_map<int64_t, ExtentIOInfo> &replace_map) override;
  int shrink(const int64_t shrink_extent_count) override;

  // statistic relative function
  bool is_free() override;
  int get_data_file_stats(
      std::vector<DataFileStatistics> &data_file_stats) override;

  // recovery relative function
  int add_data_file(DataFile *data_file) override;
  int rebuild() override;

 private:
  int find_allocatable_data_file(DataFile *&allocatable_data_file);
  DataFile *get_data_file(const int32_t file_number);
  int create_data_file();
  int add_new_data_file(DataFile *data_file);
  int delete_data_file(DataFile *data_file);
  int allocate_extent(ExtentIOInfo &io_info);
  int alloc_extent_from_data_file(DataFile *data_file, ExtentIOInfo &io_info);
  int recycle_extent_to_data_file(DataFile *data_file,
                                  const ExtentId extent_id);
  int move_to_full_map(DataFile *data_file);
  int move_to_not_full_map(DataFile *data_file);
  int move_one_extent_to_front(DataFile *src_data_file,
                               const ExtentId origin_extent_id,
                               char *extent_buf, ExtentIOInfo &new_io_info);
  int64_t calc_tail_continuous_free_extent_count() const;
  int shrink_data_file(DataFile *data_file, int64_t &left_shrink_extent_count);
  bool need_shrink_extent_space(const ShrinkCondition &shrink_condition,
                                const int64_t total_extent_count,
                                const int64_t free_extent_count);
  void calc_shrink_info(const ShrinkCondition &shrink_condition,
                        const int64_t total_extent_count,
                        const int64_t free_extent_count,
                        ShrinkInfo &shrink_info);
  int64_t get_free_extent_count();
  int64_t get_total_extent_count();
  void update_last_alloc_ts() { last_alloc_ts_ = env_->NowMicros(); }
  bool reach_shrink_allocate_interval(const int64_t shrink_allocate_interval) {
    return (env_->NowMicros() - last_alloc_ts_) >
           static_cast<uint64_t>(shrink_allocate_interval);
  }

 private:
  bool is_inited_;
  util::Env *env_;
  const util::EnvOptions &env_options_;
  int64_t table_space_id_;
  int32_t extent_space_type_;
  std::string extent_space_path_;
  std::unordered_map<int64_t, DataFile *> all_data_file_map_;
  std::unordered_map<int64_t, DataFile *> full_data_file_map_;
  std::map<int64_t, DataFile *> not_full_data_file_map_;
  uint64_t last_alloc_ts_;  // timestamp of last allocate extent
};

}  // namespace storage
}  // namespace smartengine

#endif
