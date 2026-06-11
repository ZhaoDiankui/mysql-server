/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012, Monty Program Ab

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

#ifndef SMARTENGINE_PARALLEL_DDL_CTX_H_
#define SMARTENGINE_PARALLEL_DDL_CTX_H_

#include <future>
#include "table.h"
#include "se_index_merge.h"
#include "util/se_buff.h"

namespace smartengine
{
class ha_smartengine;

struct ParallelScanCtx
{
  ParallelScanCtx(ha_smartengine* h);
  virtual ~ParallelScanCtx();
  virtual int init();
  virtual void destroy();

  uchar *pk_packed_tuple;
  uchar *sk_packed_tuple;
  uchar *pack_buffer;
  SeStringWriter sk_tails;
  /* temporary TABLE for each thread */
  TABLE thd_table;
  ha_smartengine* ha;
  const char *const table_name;
  const char *const db_name;
  ha_rows checksums;
  ha_rows rows;
  /* used to check whether unique attribute is ok */
  String last_key;
  String first_key;

  String primary_key;
  String secondary_key;
  std::string record;

  SeStringWriter pk_unpack_info;
  String new_storage_record;
};

struct ParallelDDLScanCtx : public ParallelScanCtx
{
  ParallelDDLScanCtx(ha_smartengine* h,
                     const std::shared_ptr<SeIndexMerge>& merge,
                     size_t max_partition_num)
      : ParallelScanCtx(h),
        se_merge(merge),
        bg_merge(merge, max_partition_num)
  {}
  
  virtual ~ParallelDDLScanCtx() override {}

  virtual int init() override;

  std::shared_ptr<SeIndexMerge> se_merge;
  SeIndexMerge::Bg_merge bg_merge;
};

class ParallelDDLMergeCtx
{
 public:
  ParallelDDLMergeCtx(
      std::vector<std::shared_ptr<ParallelDDLScanCtx>>& ddl_ctx_set,
      std::function<int(size_t)> merge_func)
      : m_ddl_ctx_set(ddl_ctx_set), m_merge_func(merge_func) {}
  ~ParallelDDLMergeCtx();
  void start(size_t max_threads, bool inject_err);
  int finish();

 private:
  std::vector<std::future<int>> m_merges;
  std::vector<std::shared_ptr<ParallelDDLScanCtx>>& m_ddl_ctx_set;
  std::function<int(size_t)> m_merge_func;
};

} // namespace smartengine

#endif // end of SMARTENGINE_PARALLEL_DDL_CTX_H_
