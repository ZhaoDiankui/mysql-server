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

#ifndef SMARTENGINE_INCLUDE_EXTENT_SPACE_H_
#define SMARTENGINE_INCLUDE_EXTENT_SPACE_H_

#include "storage/data_file.h"

namespace smartengine
{
namespace storage
{
class ExtentSpace
{
public:
  virtual ~ExtentSpace() = default;

  virtual void destroy() = 0;

  //ddl relative function
  virtual int create(const CreateExtentSpaceArgs &arg) = 0;
  virtual int remove() = 0;

  //extent relative function
  virtual int allocate(const std::string prefix, ExtentIOInfo &io_info) = 0;
  virtual int recycle(const std::string prefix, const ExtentId extent_id) = 0;
  // mark the extent used, only used during recovery
  virtual int reference_if_need(const std::string prefix,
                                const ExtentId extent_id,
                                ExtentIOInfo &io_info,
                                bool &existed) = 0;

  //shrink relative function
  virtual int get_shrink_info_if_need(const ShrinkCondition &shrink_condition, bool &need_shrink, ShrinkInfo &shrink_info) = 0;
  virtual int move_extens_to_front(const int64_t move_extent_count, std::unordered_map<int64_t, ExtentIOInfo> &replace_map) = 0;
  virtual int shrink(const int64_t shrink_extent_count) = 0;

  //statistic relative function
  virtual bool is_free() = 0;
  virtual int get_data_file_stats(std::vector<DataFileStatistics> &data_file_stats) = 0;

  //recovery relative function
  virtual int add_data_file(DataFile *data_file) = 0;
  virtual int rebuild() = 0;
};

} //namespace storage
} //namespace smartengine

#endif
