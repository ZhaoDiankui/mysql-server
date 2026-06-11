/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012,2013 Monty Program Ab

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

#include "se_parallel_ddl_ctx.h"
#include "current_thd.h"
#include "sql_base.h"
#include "handler/ha_smartengine.h"

namespace smartengine
{

ParallelScanCtx::ParallelScanCtx(ha_smartengine* h)
    : pk_packed_tuple(nullptr),
      sk_packed_tuple(nullptr),
      pack_buffer(),
      ha(h),
      table_name(h->table->s->table_name.str),
      db_name(h->table->s->db.str),
      checksums(0),
      rows(0)
  {}

ParallelScanCtx::~ParallelScanCtx()
{
  destroy();
}

int ParallelScanCtx::init()
{
  if (!(pk_packed_tuple = reinterpret_cast<uchar *>(my_malloc(
            PSI_NOT_INSTRUMENTED, ha->m_pack_key_len, MYF(0)))) ||
      !(sk_packed_tuple = reinterpret_cast<uchar *>(my_malloc(
            PSI_NOT_INSTRUMENTED, ha->m_max_packed_sk_len, MYF(0)))) ||
      !(pack_buffer = reinterpret_cast<uchar *>(my_malloc(
            PSI_NOT_INSTRUMENTED, ha->m_max_packed_sk_len, MYF(0))))) {
    assert(0);
    return 1;
  }

  if (open_table_from_share(current_thd, ha->table->s, table_name, 0,
                            EXTRA_RECORD | DELAYED_OPEN | SKIP_NEW_HANDLER, 0,
                            &thd_table, false, nullptr)) {
    assert(0);
    return 1;
  }
  return 0;
}

void ParallelScanCtx::destroy()
{
  my_free(pk_packed_tuple);
  pk_packed_tuple = nullptr;

  my_free(sk_packed_tuple);
  sk_packed_tuple = nullptr;

  my_free(pack_buffer);
  pack_buffer = nullptr;

  closefrm(&thd_table, false);
}

int ParallelDDLScanCtx::init()
{
  int res = HA_EXIT_SUCCESS;
  if ((res = ParallelScanCtx::init())) {
    __HANDLER_LOG(ERROR, "SEDDL: ParallelScanCtx init failed, errcode=%d, table_name: %s", res, table_name);
    assert(0);
  } else if ((res = se_merge->init())) {
    __HANDLER_LOG(ERROR, "SEDDL: se_merge init failed, errcode=%d, table_name: %s", res, table_name);
  } else if ((res = bg_merge.init())) {
    __HANDLER_LOG(ERROR, "SEDDL: bg_merge init failed, errcode=%d, table_name: %s", res, table_name);
  }
  return res;
}

ParallelDDLMergeCtx::~ParallelDDLMergeCtx()
{
  for (auto& t : m_merges) {
    if (t.valid()) {
      t.wait();
    }
  }
}

void ParallelDDLMergeCtx::start(size_t max_threads, bool inject_err)
{
  auto f = [this, inject_err] (size_t thread_id) {
    int res = (inject_err && thread_id == this->m_ddl_ctx_set.size() - 1) ? HA_ERR_INTERNAL_ERROR
                                                                          : HA_EXIT_SUCCESS;
    if (res || (res = this->m_merge_func(thread_id))) {
      // if one global merge fail, interrupt all the others
      for (auto &ctx : this->m_ddl_ctx_set) {
        ctx->bg_merge.set_interrupt();
      }
    }

    return res;
  };
  for (size_t i = 0; i < max_threads; i++) {
    m_merges.push_back(std::async(std::launch::async, f, i));
  }
}

int ParallelDDLMergeCtx::finish()
{
  int res = HA_EXIT_SUCCESS;
  for (size_t i = 0; i < m_merges.size(); i++) {
    if ((res = m_merges[i].get())) {
      return res;
    }
  }
  return res;
}

} // namespace smartengine
