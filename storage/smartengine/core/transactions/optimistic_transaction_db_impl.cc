//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "transactions/optimistic_transaction_db_impl.h"

#include <string>
#include <vector>
#include "options/options.h"
#include "transactions/optimistic_transaction_impl.h"

using namespace smartengine::common;
using namespace smartengine::db;

namespace smartengine {
namespace util {

Transaction* OptimisticTransactionDBImpl::BeginTransaction(
    const WriteOptions& write_options,
    const OptimisticTransactionOptions& txn_options, Transaction* old_txn) {
  if (old_txn != nullptr) {
    ReinitializeTransaction(old_txn, write_options, txn_options);
    return old_txn;
  } else {
    return new OptimisticTransactionImpl(this, write_options, txn_options);
  }
}

Status OptimisticTransactionDB::Open(const Options &options,
                                     const std::string &db_name,
                                     std::vector<ColumnFamilyHandle *> *handles,
                                     OptimisticTransactionDB **db)
{
  Status status;
  DB *db_ptr = nullptr;

  status = DB::Open(options, db_name, handles, &db_ptr);
  if (status.ok()) {
    *db = new OptimisticTransactionDBImpl(db_ptr);
  }

  return status;
}

void OptimisticTransactionDBImpl::ReinitializeTransaction(
    Transaction* txn, const WriteOptions& write_options,
    const OptimisticTransactionOptions& txn_options) {
  assert(dynamic_cast<OptimisticTransactionImpl*>(txn) != nullptr);
  auto txn_impl = reinterpret_cast<OptimisticTransactionImpl*>(txn);

  txn_impl->Reinitialize(this, write_options, txn_options);
}

}  //  namespace util
}  //  namespace smartengine