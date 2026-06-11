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

#ifndef SMARTENGINE_TRANSACTION_WRITE_BATCH_IMPL_H_
#define SMARTENGINE_TRANSACTION_WRITE_BATCH_IMPL_H_

#include "se_transaction.h"
#include "handler/se_hton.h"
#include "plugin/se_system_vars.h"
#include "write_batch/write_batch_with_index.h"

namespace smartengine
{

/* This is a se write batch. This class doesn't hold or wait on any
   transaction locks (skips se transaction API) thus giving better
   performance. The commit is done through se_db->GetBaseDB()->Commit().

   Currently this is only used for replication threads which are guaranteed
   to be non-conflicting. Any further usage of this class should completely
   be thought thoroughly.
*/
class SeWritebatchImpl : public SeTransaction
{
public:
  explicit SeWritebatchImpl(THD *const thd);

  virtual ~SeWritebatchImpl() override;

  virtual common::Status put(db::ColumnFamilyHandle *const column_family,
                             const common::Slice &key,
                             const common::Slice &value) override;

  virtual common::Status delete_key(db::ColumnFamilyHandle *const column_family,
                                    const common::Slice &key) override;

  virtual common::Status single_delete(db::ColumnFamilyHandle *const column_family,
                                       const common::Slice &key) override;

  virtual common::Status get(db::ColumnFamilyHandle *const column_family,
                             const common::Slice &key,
                             std::string *const value) const override;

  virtual common::Status get_latest(db::ColumnFamilyHandle *const column_family,
                                    const common::Slice &key,
                                    std::string *const value) const override;

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

  virtual void start_stmt() override { m_batch->SetSavePoint(); }

  virtual void rollback_stmt() override;

  virtual void acquire_snapshot(bool acquire_now) override;

  virtual void release_snapshot() override;

  virtual void release_lock(db::ColumnFamilyHandle *const column_family, const std::string &rowkey) override
  {
    /**Nothing to do here since we don't hold any row locks.*/
  }

  virtual bool has_modifications() const override { return m_batch->GetWriteBatch()->Count() > 0; }

  virtual bool is_writebatch_trx() const override { return true; }

  virtual db::WriteBatchBase *get_indexed_write_batch() override;

  virtual db::Iterator *get_iterator(const common::ReadOptions &options,
                                     db::ColumnFamilyHandle *const column_family) override;

  virtual void detach_from_se() override {}

  virtual void set_lock_timeout(int timeout_sec_arg) override { /**Nothing to do here.*/ }

  virtual void set_sync(bool sync) override { write_opts.sync = sync; }

  virtual db::WriteBatchBase *get_write_batch() override { return m_batch; }

  virtual bool is_tx_started() const override { return (m_batch != nullptr); }

private:
  virtual bool prepare(const util::TransactionName &name) override;

  virtual bool commit_no_binlog() override;

  virtual bool commit_no_binlog2() override;

  // Called after commit/rollback.
  void reset();

private:
  util::WriteBatchWithIndex *m_batch;

  common::WriteOptions write_opts;
};

} // namespace smartengine

#endif // end of SMARTENGINE_TRANSACTION_WRITE_BATCH_IMPL_H_
