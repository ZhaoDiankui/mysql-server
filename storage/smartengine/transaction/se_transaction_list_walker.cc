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

#include "se_transaction_list_walker.h"
#include "mysql/plugin.h"
#include "se_buff.h"
#include "se_transaction_impl.h"
#include "se_write_batch_impl.h"
#include "sql_class.h"
#include "sql_thd_internal_api.h"
#include "transactions/transaction_db_impl.h"

namespace smartengine
{
extern util::TransactionDB *se_db;

std::string SeSnapshotStatus::current_timestamp()
{
  static const char *const format = "%d-%02d-%02d %02d:%02d:%02d";
  time_t currtime;
  struct tm currtm;

  time(&currtime);

  localtime_r(&currtime, &currtm);

  return format_string(format, currtm.tm_year + 1900, currtm.tm_mon + 1,
                       currtm.tm_mday, currtm.tm_hour, currtm.tm_min,
                       currtm.tm_sec);
}

std::string SeSnapshotStatus::get_header()
{
  return "\n============================================================\n" +
         current_timestamp() +
         " se TRANSACTION MONITOR OUTPUT\n"
         "============================================================\n"
         "---------\n"
         "SNAPSHOTS\n"
         "---------\n"
         "LIST OF SNAPSHOTS FOR EACH SESSION:\n";
}

std::string SeSnapshotStatus::get_footer()
{
  return "-----------------------------------------\n"
         "END OF se TRANSACTION MONITOR OUTPUT\n"
         "=========================================\n";
}

/* Implement SeTransaction interface */
/* Create one row in the snapshot status table */
void SeSnapshotStatus::process_trans(const SeTransaction *const tx)
{
  assert(tx != nullptr);

  /* Calculate the duration the snapshot has existed */
  int64_t snapshot_timestamp = tx->m_snapshot_timestamp;
  if (snapshot_timestamp != 0) {
    int64_t curr_time;
    se_db->GetEnv()->GetCurrentTime(&curr_time);

    THD *thd = tx->get_thd();
    char buffer[1024];
    thd_security_context(thd, buffer, sizeof buffer, 0);
    m_data += format_string("---SNAPSHOT, ACTIVE %lld sec\n"
                            "%s\n"
                            "lock count %llu, write count %llu\n",
                            curr_time - snapshot_timestamp, buffer,
                            tx->get_lock_count(), tx->get_write_count());
  }
}

void SeTrxInfoAggregator::process_trans(const SeTransaction *const tx)
{
  static const std::map<int, std::string> state_map = {
      {util::Transaction::STARTED, "STARTED"},
      {util::Transaction::AWAITING_PREPARE, "AWAITING_PREPARE"},
      {util::Transaction::PREPARED, "PREPARED"},
      {util::Transaction::AWAITING_COMMIT, "AWAITING_COMMIT"},
      {util::Transaction::COMMITED, "COMMITED"},
      {util::Transaction::AWAITING_ROLLBACK, "AWAITING_ROLLBACK"},
      {util::Transaction::ROLLEDBACK, "ROLLEDBACK"},
  };
  static const size_t trx_query_max_len = 1024;  // length stolen from InnoDB

  assert(tx != nullptr);

  THD *const thd = tx->get_thd();
  ulong thread_id = thd_thread_id(thd);

  if (tx->is_writebatch_trx()) {
    const auto wb_impl = static_cast<const SeWritebatchImpl *>(tx);
    assert(wb_impl);
    m_trx_info->push_back(
        {"",                            /* name */
         0,                             /* trx_id */
         wb_impl->get_write_count(), 0, /* lock_count */
         0,                             /* timeout_sec */
         "",                            /* state */
         "",                            /* waiting_key */
         0,                             /* waiting_cf_id */
         1,                             /*is_replication */
         1,                             /* skip_trx_api */
         wb_impl->is_tx_read_only(), 0, /* deadlock detection */
         wb_impl->num_ongoing_bulk_load(), thread_id, "" /* query string */});
  } else {
    const auto tx_impl = static_cast<const SeTransactionImpl *>(tx);
    assert(tx_impl);
    const util::Transaction *se_trx = tx_impl->get_se_trx();

    if (se_trx == nullptr) {
      return;
    }

    char query[trx_query_max_len + 1];
    std::string query_str;
    if (thd_query_safe(thd, query, trx_query_max_len) > 0) {
      query_str.assign(query);
    }

    const auto state_it = state_map.find(se_trx->GetState());
    assert(state_it != state_map.end());
    const int is_replication = (thd->rli_slave != nullptr);
    uint32_t waiting_cf_id;
    std::string waiting_key;
    se_trx->GetWaitingTxns(&waiting_cf_id, &waiting_key),

        m_trx_info->push_back(
            {se_trx->GetName(), se_trx->GetID(), tx_impl->get_write_count(),
             tx_impl->get_lock_count(), tx_impl->get_timeout_sec(),
             state_it->second, waiting_key, waiting_cf_id, is_replication,
             0, /* skip_trx_api */
             tx_impl->is_tx_read_only(), se_trx->IsDeadlockDetect(),
             tx_impl->num_ongoing_bulk_load(), thread_id, query_str});
  }
}
} // namespace smartengine
