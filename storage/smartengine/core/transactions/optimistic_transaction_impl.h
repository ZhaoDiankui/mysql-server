// Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "db/write_callback.h"
#include "transactions/optimistic_transaction_db.h"
#include "transactions/transaction.h"
#include "transactions/transaction_base.h"
#include "transactions/transaction_util.h"
#include "db/db.h"
#include "write_batch/write_batch_with_index.h"

namespace smartengine {
namespace util {

class OptimisticTransactionImpl : public TransactionBaseImpl {
 public:
  OptimisticTransactionImpl(
      util::OptimisticTransactionDB* db,
      const common::WriteOptions& write_options,
      const util::OptimisticTransactionOptions& txn_options);

  virtual ~OptimisticTransactionImpl() override;

  void Reinitialize(util::OptimisticTransactionDB* txn_db,
                    const common::WriteOptions& write_options,
                    const util::OptimisticTransactionOptions& txn_options);

  common::Status Prepare() override;

  common::Status Commit() override;

  common::Status CommitAsync(common::AsyncCallback* call_back) override;

  common::Status Rollback() override;

  common::Status SetName(const TransactionName& name) override;

 protected:
  common::Status TryLock(db::ColumnFamilyHandle* column_family,
                         const common::Slice& key, bool read_only,
                         bool exclusive, bool untracked = false, 
                         bool lock_uk = false, const common::ReadOptions *options = nullptr) override;

 private:
  util::OptimisticTransactionDB* const txn_db_;

  friend class OptimisticTransactionCallback;

  void Initialize(const util::OptimisticTransactionOptions& txn_options);

  // Returns OK if it is safe to commit this transaction.  Returns
  // common::Status::Busy
  // if there are read or write conflicts that would prevent us from committing
  // OR if we can not determine whether there would be any such conflicts.
  //
  // Should only be called on writer thread.
  common::Status CheckTransactionForConflicts(db::DB* db);

  void Clear() override;

  void UnlockGetForUpdate(db::ColumnFamilyHandle* column_family,
                          const common::Slice& key) override {
    // Nothing to unlock.
  }

  // No copying allowed
  OptimisticTransactionImpl(const OptimisticTransactionImpl&);
  void operator=(const OptimisticTransactionImpl&);
};

// Used at commit time to trigger transaction validation
class OptimisticTransactionCallback : public db::WriteCallback {
 public:
  explicit OptimisticTransactionCallback(OptimisticTransactionImpl* txn)
      : txn_(txn) {}

  common::Status Callback(db::DB* db) override {
    return txn_->CheckTransactionForConflicts(db);
  }

  bool AllowWriteBatching() override { return false; }

 private:
  OptimisticTransactionImpl* txn_;
};

}  //  namespace util
}  //  namespace smartengine