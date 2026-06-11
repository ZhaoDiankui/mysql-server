/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#include <map>
#include <string>
#include "db/db.h"

#ifdef _WIN32
// Windows API macro interference
#undef DeleteFile
#endif

namespace smartengine {
namespace db {
class ColumnFamilyHandle;
struct MiniTables;
struct CreateSubTableArgs;
}

namespace util {

using smartengine::db::ColumnFamilyHandle;
using smartengine::db::MiniTables;
using smartengine::db::Snapshot;
using smartengine::db::Range;
using smartengine::db::Iterator;

// This class contains APIs to stack DB wrappers.Eg. Stack TTL over base db
class StackableDB : public db::DB {
 public:
  // StackableDB is the owner of db now!
  explicit StackableDB(DB* db) : db_(db) {}

  ~StackableDB() override
  { 
    MOD_DELETE_OBJECT(DB, db_);
  }

  virtual DB* GetBaseDB() { return db_; }

  virtual DB* GetRootDB() override { return db_->GetRootDB(); }

  virtual common::Status CreateColumnFamily(db::CreateSubTableArgs &args, ColumnFamilyHandle** handle) override {
    return db_->CreateColumnFamily(args, handle);
  }

  virtual common::Status DropColumnFamily(
      ColumnFamilyHandle* column_family) override {
    return db_->DropColumnFamily(column_family);
  }

  virtual int modify_table_schema(ColumnFamilyHandle *subtable_handle, const schema::TableSchema &table_schema) override
  {
    return db_->modify_table_schema(subtable_handle, table_schema);
  }

  virtual common::Status Put(const common::WriteOptions& options,
                             ColumnFamilyHandle* column_family,
                             const common::Slice& key,
                             const common::Slice& val) override {
    return db_->Put(options, column_family, key, val);
  }

  using DB::Get;
  virtual common::Status Get(const common::ReadOptions& options,
                             ColumnFamilyHandle* column_family,
                             const common::Slice& key,
                             common::PinnableSlice* value) override {
    return db_->Get(options, column_family, key, value);
  }

  using DB::InstallSstExternal;
  virtual common::Status InstallSstExternal(ColumnFamilyHandle* column_family,
                                            MiniTables* mtables) override {
    return db_->InstallSstExternal(column_family, mtables);
  }

  using DB::Delete;
  virtual common::Status Delete(const common::WriteOptions& wopts,
                                ColumnFamilyHandle* column_family,
                                const common::Slice& key) override {
    return db_->Delete(wopts, column_family, key);
  }

  using DB::SingleDelete;
  virtual common::Status SingleDelete(const common::WriteOptions& wopts,
                                      ColumnFamilyHandle* column_family,
                                      const common::Slice& key) override {
    return db_->SingleDelete(wopts, column_family, key);
  }

  virtual common::Status Write(const common::WriteOptions& opts,
                               db::WriteBatch* updates) override {
    return db_->Write(opts, updates);
  }

  using DB::NewIterator;
  virtual Iterator* NewIterator(const common::ReadOptions& opts,
                                ColumnFamilyHandle* column_family) override {
    return db_->NewIterator(opts, column_family);
  }

  virtual const Snapshot* GetSnapshot() override { return db_->GetSnapshot(); }

  virtual void ReleaseSnapshot(const Snapshot* snapshot) override {
    return db_->ReleaseSnapshot(snapshot);
  }

  using DB::GetMapProperty;
  using DB::GetProperty;
  virtual bool GetProperty(ColumnFamilyHandle* column_family,
                           const common::Slice& property,
                           std::string* value) override {
    return db_->GetProperty(column_family, property, value);
  }
  virtual bool GetMapProperty(ColumnFamilyHandle* column_family,
                              const common::Slice& property,
                              std::map<std::string, double>* value) override {
    return db_->GetMapProperty(column_family, property, value);
  }

  using DB::GetIntProperty;
  virtual bool GetIntProperty(ColumnFamilyHandle* column_family,
                              const common::Slice& property,
                              uint64_t* value) override {
    return db_->GetIntProperty(column_family, property, value);
  }

  using DB::GetAggregatedIntProperty;
  virtual bool GetAggregatedIntProperty(const common::Slice& property,
                                        uint64_t* value) override {
    return db_->GetAggregatedIntProperty(property, value);
  }

  using DB::GetApproximateSizes;
  virtual void GetApproximateSizes(
      ColumnFamilyHandle* column_family, const Range* r, int n, uint64_t* sizes,
      uint8_t include_flags = INCLUDE_FILES) override {
    return db_->GetApproximateSizes(column_family, r, n, sizes, include_flags);
  }

  using DB::GetApproximateMemTableStats;
  virtual void GetApproximateMemTableStats(ColumnFamilyHandle* column_family,
                                           const Range& range,
                                           uint64_t* const count,
                                           uint64_t* const size) override {
    return db_->GetApproximateMemTableStats(column_family, range, count, size);
  }

  using DB::CompactRange;
  virtual common::Status CompactRange( ColumnFamilyHandle* column_family,
                                       const uint32_t manual_compact_type) override
  {
    return db_->CompactRange(column_family, manual_compact_type);
  }

  virtual common::Status CompactRange(const uint32_t compact_type) override
  {
    return db_->CompactRange(compact_type);
  }

  virtual common::Status PauseBackgroundWork() override {
    return db_->PauseBackgroundWork();
  }

  virtual common::Status ContinueBackgroundWork() override {
    return db_->ContinueBackgroundWork();
  }

  virtual void CancelAllBackgroundWork(bool wait) override {
    return db_->CancelAllBackgroundWork(wait);
  }

  virtual common::Status EnableAutoCompaction(
      const std::vector<ColumnFamilyHandle*>& column_family_handles) override {
    return db_->EnableAutoCompaction(column_family_handles);
  }

  virtual const std::string& GetName() const override { return db_->GetName(); }

  virtual util::Env* GetEnv() const override { return db_->GetEnv(); }

  using DB::GetDBOptions;
  virtual common::DBOptions GetDBOptions() const override {
    return db_->GetDBOptions();
  }

  using DB::Flush;
  virtual common::Status Flush(const common::FlushOptions& fopts,
                               ColumnFamilyHandle* column_family) override {
    return db_->Flush(fopts, column_family);
  }

  virtual int sync_wal() override { return db_->sync_wal(); }

  virtual common::Status DisableFileDeletions() override {
    return db_->DisableFileDeletions();
  }

  virtual common::Status EnableFileDeletions(bool force) override {
    return db_->EnableFileDeletions(force);
  }

  virtual common::SequenceNumber GetLatestSequenceNumber() const override {
    return db_->GetLatestSequenceNumber();
  }

  virtual int reset_pending_shrink(const uint64_t subtable_id) override {
    return db_->reset_pending_shrink(subtable_id);
  }

  using DB::SetOptions;
  virtual common::Status SetOptions(
      const std::unordered_map<std::string, std::string>& new_options) override {
    return db_->SetOptions(new_options);
  }

  virtual common::Status SetOptions(
      ColumnFamilyHandle* column_family_handle,
      const std::unordered_map<std::string, std::string>& new_options)
      override {
    return db_->SetOptions(column_family_handle, new_options);
  }

  virtual common::Status SetDBOptions(
      const std::unordered_map<std::string, std::string>& new_options)
      override {
    return db_->SetDBOptions(new_options);
  }

  virtual ColumnFamilyHandle* DefaultColumnFamily() const override {
    return db_->DefaultColumnFamily();
  }

  virtual int shrink_table_space(int32_t table_space_id) override {
    return db_->shrink_table_space(table_space_id);
  }

  virtual int get_all_subtable(
    std::vector<smartengine::db::ColumnFamilyHandle*> &subtables) const override {
    return db_->get_all_subtable(subtables);
  }

  virtual int return_all_subtable(std::vector<smartengine::db::ColumnFamilyHandle*> &subtables) override {
    return db_->return_all_subtable(subtables);
  }

  virtual db::SuperVersion *GetAndRefSuperVersion(db::ColumnFamilyData *cfd) override {
    return db_->GetAndRefSuperVersion(cfd);
  }

  virtual void ReturnAndCleanupSuperVersion(db::ColumnFamilyData *cfd, db::SuperVersion *sv) override {
    db_->ReturnAndCleanupSuperVersion(cfd, sv);
  }

  virtual int get_data_file_stats(std::vector<storage::DataFileStatistics> &data_file_stats) override
  {
    return db_->get_data_file_stats(data_file_stats);
  }

  virtual std::list<storage::CompactionJobStatsInfo*> &get_compaction_history(std::mutex **mu,
          storage::CompactionJobStatsInfo **sum) override {
    return db_->get_compaction_history(mu, sum);
  }

  bool get_columnfamily_stats(ColumnFamilyHandle* column_family, int64_t &data_size,
                               int64_t &num_entries, int64_t &num_deletes, int64_t &disk_size) override {
    return db_->get_columnfamily_stats(column_family, data_size, num_entries, num_deletes, disk_size);
  }

 protected:
  DB* db_;
};

}  // namespace db
}  // namespace smartengine
