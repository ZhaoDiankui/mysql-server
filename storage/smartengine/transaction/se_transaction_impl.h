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

#ifndef SMARTENGINE_TRANSACTION_IMPL_H_
#define SMARTENGINE_TRANSACTION_IMPL_H_

#include "se_transaction.h"
#include "handler/se_hton.h"
#include "plugin/se_system_vars.h"

namespace smartengine
{
/*
  This is a se transaction. Its members represent the current transaction,
  which consists of:
  - the snapshot
  - the changes we've made but are not seeing yet.

  The changes are made to individual tables, which store them here and then
  this object commits them on commit.
*/
class SeTransactionImpl : public SeTransaction {
public:
  explicit SeTransactionImpl(THD *const thd);

  virtual ~SeTransactionImpl() override;

  virtual common::Status put(db::ColumnFamilyHandle *const column_family,
                             const common::Slice &key,
                             const common::Slice &value) override;

  virtual common::Status delete_key(db::ColumnFamilyHandle *const column_family,
                                    const common::Slice &key) override;

  virtual common::Status single_delete(db::ColumnFamilyHandle *const column_family,
                                       const common::Slice &key) override;

  virtual common::Status get(db::ColumnFamilyHandle *const column_family,
                             const common::Slice &key,
                             std::string *value) const override;

  virtual common::Status get_latest(db::ColumnFamilyHandle *const column_family,
                                    const common::Slice &key,
                                    std::string *value) const override;

  virtual common::Status get_for_update(db::ColumnFamilyHandle *const column_family,
                                        const common::Slice &key,
                                        std::string *const value,
                                        bool exclusive,
                                        bool only_lock = false) override;

  virtual common::Status lock_unique_key(db::ColumnFamilyHandle *const column_family,
                                         const common::Slice &key,
                                         const bool skip_bloom_filter,
                                         const bool fill_cache,
                                         const bool exclusive) override;

  virtual void rollback() override;

  virtual void start_tx() override;

  /*
    Start a statement inside a multi-statement transaction.

    @todo: are we sure this is called once (and not several times) per
    statement start?

    For hooking to start of statement that is its own transaction, see
    ha_smartengine::external_lock().
  */
  virtual void start_stmt() override;

  /*
    This must be called when last statement is rolled back, but the transaction
    continues
  */
  virtual void rollback_stmt() override;

  virtual void acquire_snapshot(bool acquire_now) override;

  virtual void release_snapshot() override;

  virtual void release_lock(db::ColumnFamilyHandle *const column_family, const std::string &rowkey) override;

  virtual bool is_tx_started() const override { return (m_se_tx != nullptr); }

  virtual bool is_writebatch_trx() const override { return false; }

  virtual bool has_modifications() const override;

  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes WILL be visible to the transaction.
  */
  virtual db::WriteBatchBase *get_indexed_write_batch() override;

  virtual void set_lock_timeout(int timeout_sec_arg) override;

  virtual void set_sync(bool sync) override;

  virtual void detach_from_se() override;

  virtual db::WriteBatchBase *get_write_batch() override;

  virtual db::Iterator *get_iterator(const common::ReadOptions &options, db::ColumnFamilyHandle *const column_family) override;

  bool is_async_commit() { return m_se_tx->GetWriteOptions()->async_commit; }

  bool has_snapshot() { return m_read_opts.snapshot != nullptr; }

  const util::Transaction *get_se_trx() const { return m_se_tx; }

private:
  virtual bool prepare(const util::TransactionName &name) override;

  virtual bool commit_no_binlog() override;

  virtual bool commit_no_binlog2() override;

  void release_tx(void);

private:
  util::Transaction *m_se_tx = nullptr;
  util::Transaction *m_se_reuse_tx = nullptr;
};

} // namespace smartengine

#endif
