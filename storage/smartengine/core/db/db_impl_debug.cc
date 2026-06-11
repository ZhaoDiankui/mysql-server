//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef NDEBUG

#include "db/db_impl.h"
#include "monitoring/thread_status_updater.h"
#include "storage/storage_logger.h"


namespace smartengine
{
using namespace common;
using namespace util;
using namespace monitor;
using namespace table;
using namespace storage;
namespace db
{

uint64_t DBImpl::TEST_GetLevel0TotalSize() {
  InstrumentedMutexLock l(&mutex_);
  return 0;
}

void DBImpl::TEST_HandleWALFull() {
  WriteContext write_context;
  InstrumentedMutexLock l(&mutex_);
  HandleWALFull(&write_context);
}

int64_t DBImpl::TEST_MaxNextLevelOverlappingBytes(
    ColumnFamilyHandle* column_family) {
  return 0;
}

void DBImpl::TEST_GetFilesMetaData(
    ColumnFamilyHandle* column_family,
    std::vector<std::vector<FileMetaData>>* metadata) {
}

Status DBImpl::TEST_FlushMemTable(bool wait, ColumnFamilyHandle* cfh) {
  FlushOptions fo;
  fo.wait = wait;
  ColumnFamilyData* cfd;
  if (cfh == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfhi = reinterpret_cast<ColumnFamilyHandleImpl*>(cfh);
    cfd = cfhi->cfd();
  }
  return flush_memtable(cfd, fo);
}

Status DBImpl::TEST_WaitForFlushMemTable(ColumnFamilyHandle* column_family) {
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
    cfd = cfh->cfd();
  }
  return WaitForFlushMemTable(cfd);
}

Status DBImpl::TEST_WaitForCompact() {
  // Wait until the compaction completes

  // TODO: a bug here. This function actually does not necessarily
  // wait for compact. It actually waits for scheduled compaction
  // OR flush to finish.

  InstrumentedMutexLock l(&mutex_);
  while ((bg_compaction_scheduled_ || bg_flush_scheduled_) && bg_error_.ok()) {
    bg_cv_.Wait();
  }
  return bg_error_;
}

void DBImpl::TEST_LockMutex() { mutex_.Lock(); }

void DBImpl::TEST_UnlockMutex() { mutex_.Unlock(); }

uint64_t DBImpl::TEST_LogfileNumber() {
  InstrumentedMutexLock l(&mutex_);
  return logfile_number_;
}

uint64_t DBImpl::TEST_FindMinLogContainingOutstandingPrep() {
  return FindMinLogContainingOutstandingPrep();
}

uint64_t DBImpl::TEST_FindMinPrepLogReferencedByMemTable() {
  return FindMinPrepLogReferencedByMemTable();
}

int DBImpl::TEST_create_subtable(const ColumnFamilyDescriptor &cf, int32_t tid, ColumnFamilyHandle *&handle)
{
  int ret = Status::kOk;
  const int64_t TEST_INDEX_ID_BASE = 100;
  schema::TableSchema table_schema;
  table_schema.set_index_id(tid);
  common::ColumnFamilyOptions cf_options;
  CreateSubTableArgs args(table_schema, cf_options, true, tid);

  if (FAILED(CreateColumnFamily(args, &handle).code())) {
    SE_LOG(WARN, "fail to create subtable", K(ret));
  }

  return ret;
}

int DBImpl::TEST_modify_table_schema(ColumnFamilyHandle *index_handle)
{
  int ret = Status::kOk;
  int64_t dummy_commit_lsn = 0;
  schema::TableSchema table_schema;
  table_schema.set_index_id(index_handle->GetID());
  table_schema.set_primary_index_id(0);

  if (FAILED(StorageLogger::get_instance().begin(MODIFY_INDEX))) {
    SE_LOG(WARN, "fail to begin slog trans", K(ret));
  } else {
    if (FAILED(modify_table_schema(index_handle, table_schema))) {
      SE_LOG(WARN, "fail to modify table schema", K(ret), K(table_schema));
    } else if (FAILED(StorageLogger::get_instance().commit(dummy_commit_lsn))) {
      SE_LOG(WARN, "fail to commit slog trans", K(ret));
    }

    if (FAILED(ret)) {
      StorageLogger::get_instance().abort();
    }
  }

  return ret;
}

Status DBImpl::TEST_GetLatestMutableCFOptions(
    ColumnFamilyHandle* column_family, MutableCFOptions* mutable_cf_options) {
  InstrumentedMutexLock l(&mutex_);

  auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
  *mutable_cf_options = *cfh->cfd()->GetLatestMutableCFOptions();
  return Status::OK();
}

int DBImpl::TEST_BGCompactionsAllowed() const {
  InstrumentedMutexLock l(&mutex_);
  return BGCompactionsAllowed();
}
}
}  // namespace smartengine
#endif  // NDEBUG
