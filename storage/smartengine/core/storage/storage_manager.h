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

#ifndef SMARTENGINE_INCLUDE_STORAGE_MANAGER_H_
#define SMARTENGINE_INCLUDE_STORAGE_MANAGER_H_

#include "db/table_cache.h"
#include "db/snapshot_impl.h"
#include "storage/change_info.h"
#include "storage/large_object_extent_manager.h"
#include "table/two_level_iterator.h"
namespace smartengine
{
namespace db
{
class InternalStats;
class ColumnFamilyData;
class SnapshotImpl;
}
namespace table
{
class MergeIteratorBuilder;
}
namespace storage
{
struct LayerPosition;
struct ExtentLayer;
class ExtentLayerVersion;
class StorageManager;

struct RecycleArgs
{
  StorageManager *storage_manager_;

  RecycleArgs(StorageManager *storage_manager) : storage_manager_(storage_manager) {}
  ~RecycleArgs() {}
};

class StorageManager
{
public:
  explicit StorageManager(const db::ColumnFamilyData *owner,
                          const util::EnvOptions &env_options,
                          const common::ImmutableCFOptions &imm_cf_options,
                          const common::MutableCFOptions &mutable_cf_options);
  virtual ~StorageManager();

  int init();
  void destroy();

  int apply(const ChangeInfo &change_info, bool for_recovery);
  int get(const common::Slice &key,
          const db::Snapshot &current_meta,
          std::function<int(const ExtentMeta *extent_meta, int32_t level, bool &found)> save_value);
  int add_iterators(db::TableCache *table_cache,
                    db::InternalStats *internal_stats,
                    const common::ReadOptions &read_options,
                    table::MergeIteratorBuilder *merge_iter_builder,
                    const db::Snapshot *current_meta);
  const db::Snapshot *get_current_version() const { return current_meta_; }
  int64_t get_current_total_extent_count() const;
  // todo
//  void set_scan_add_blocks_limit(const uint64_t scan_add_blocks_limit) {
//    mutable_cf_options_.scan_add_blocks_limit = scan_add_blocks_limit;
//  }
  table::InternalIterator *get_single_level_iterator(const common::ReadOptions &read_options,
                                                     util::Arena *arena,
                                                     const db::Snapshot *current_meta,
                                                     int64_t level) const;

  int get_extent_layer_iterator(util::Arena *arena,
                                const db::Snapshot *snapshot,
                                const LayerPosition &layer_position,
                                table::InternalIterator *&iterator) const;

  int64_t level0_oldest_layer_sequence(const db::Snapshot *current_meta) const;
  int get_level_usage_percent(const db::Snapshot *current_meta,
                              const int32_t level,
                              int64_t &usage_percent,
                              int64_t &delete_size) const;
  int get_level_delete_size(const db::Snapshot *current_meta,
                            const int32_t level,
                            int64_t &delete_size) const;
  std::pair<uint64_t, uint64_t> get_level_fragmentation_rate_and_delete_percent(
      const db::Snapshot *current_meta, int32_t level) const;
  //calculate the range size
  int64_t approximate_size(const db::ColumnFamilyData *cfd,
                           const common::Slice &start,
                           const common::Slice &end,
                           int32_t start_level,
                           int32_t end_level,
                           const db::Snapshot *sn,
                           int64_t estimate_cost_depth);
  int approximate_key_offet(const common::Slice &key,
                            const db::ColumnFamilyData *cfd,
                            const ExtentId &extent_id,
                            const int32_t level,
                            int64_t &offset);
  void print_raw_meta();

  db::SnapshotImpl *accquire_meta_snapshot();
  void release_meta_snapshot(db::SnapshotImpl *meta_snapshot);
  static void release_backup_meta_snapshot(db::SnapshotImpl *meta_snapshot);
  static int cleanup_backup_snapshot(db::MetaSnapshotSet &meta_snapshots);
  void destroy_meta_snapshot(db::SnapshotImpl *meta_snapshot,
                             util::autovector<storage::ExtentLayerVersion *> &recyle_extent_layer_versions);
  // clang-format off
  static void destroy_backup_meta_snapshot(db::SnapshotImpl *meta_snapshot,
                                           util::autovector<storage::ExtentLayerVersion *> &recyle_extent_layer_versions);
  // clang-format on
  static int recycle_backup_snapshot(db::MetaSnapshotSet &meta_snapshots);
  static void async_recycle(void *args);
  int recycle();
  int recover_extent_space();
  bool get_extent_stats(int64_t &data_size, int64_t &num_entries, int64_t &num_deletes, int64_t &disk_size);
  int release_extent_resource(bool for_recovery);
  bool can_gc()
  {
    // make sure, only one version snapshot current, and all old version snapshot has been recycle
    std::lock_guard<std::mutex> meta_mutex_guard(meta_mutex_);
    return (1 == meta_snapshot_list_.count() && 0 == bg_recycle_count_ && 0 == waiting_delete_versions_.size());
  }

  bool can_shrink()
  {
    //make sure, only single version snapshot current, and all old version snapshot has been recycle
    //and the subtable not contain large object
    std::lock_guard<std::mutex> meta_mutex_guard(meta_mutex_);
    return (1 == meta_snapshot_list_.count() && 0 == bg_recycle_count_ && 0 == waiting_delete_versions_.size())
           && 0 == lob_extent_mgr_->get_lob_extent_count(); 
  }
  int deserialize_and_dump(const char *buf, int64_t buf_len, int64_t &pos,
                           char *str_buf, int64_t str_buf_len, int64_t &str_pos);
  int get_extent_positions(const int64_t index_id, ExtentPositionMap &extent_info_map);
  DECLARE_SERIALIZATION()
  DECLARE_TO_STRING()

private:
  int init_extent_layer_versions(db::InternalKeyComparator *internalkey_comparator);
  int normal_apply(const ChangeInfo &change_info);
  int apply_large_object(const ChangeInfo &change_info);
  int build_new_version(ExtentLayerVersion *old_version,
                        const ExtentChangeArray &extent_changes,
                        const ExtentChangeArray &lob_extent_change,
                        ExtentLayerVersion *&new_version);
  int prepare_build_new_version(const ExtentChangeArray &extent_changes,
                                ExtentChangesMap &extent_changes_per_layer);
  int actual_build_new_version(const ExtentLayerVersion *old_version,
                               const ExtentChangesMap &extent_changes_per_layer,
                               const ExtentChangeArray &lob_extent_change,
                               ExtentLayerVersion *&new_version);
  int copy_on_write_accord_old_version(const ExtentLayerVersion *old_version,
                                       const ExtentChangesMap &extent_changes_per_layer,
                                       const ExtentChangeArray &lob_extent_change,
                                       ExtentLayerVersion *&new_version);
  int build_new_extent_layer_if_need(const ExtentChangesMap &extent_changes_per_layer,
                                     ExtentLayerVersion *&new_version);
  int deal_dump_extent_layer_if_need(const ExtentLayerVersion *old_version,
                                     const ExtentChangesMap &extent_changes_per_layer,
                                     const ExtentChangeArray &lob_extent_change,
                                     ExtentLayerVersion *&new_version);
  int record_lob_extent_info_to_dump_extent_layer(const ExtentChangeArray &lob_extent_change, ExtentLayer *&dump_extent_layer);
  int modify_extent_layer(ExtentLayer *extent_layer, const ExtentChangeArray &extent_changes);
  int add_iterator_for_layer(const LayerPosition &layer_position,
                             db::TableCache *table_cache,
                             db::InternalStats *internal_stats,
                             const common::ReadOptions &read_options,
                             table::MergeIteratorBuilder *merge_iter_builder,
                             ExtentLayer *extent_layer);
  int create_extent_layer_iterator(util::Arena *arena,
                                   const db::Snapshot *snapshot,
                                   const LayerPosition &layer_position,
                                   table::InternalIterator *&iterator) const;
  int64_t one_layer_approximate_size(const db::ColumnFamilyData *cfd,
                                     const common::Slice &start,
                                     const common::Slice &end,
                                     int32_t level,
                                     table::InternalIterator *iter,
                                     int64_t estimate_cost_depth,
                                     EstimateCostStats &cost_stats);
  int update_current_meta_snapshot(ExtentLayerVersion **new_extent_layer_versions);
  db::SnapshotImpl *accquire_meta_snapshot_unsafe();
  db::SnapshotImpl *accquire_backup_meta_snapshot();

  void release_meta_snapshot_unsafe(db::SnapshotImpl *meta_snapshot);

  //TODO(Zhao Dongsheng): The filter logic need reconstructure.
  bool is_filter_skipped(int32_t level) { return level > 0; }
  void print_raw_meta_unsafe();
  int build_new_extent_layer(util::autovector<ExtentId> &extent_ids, ExtentLayer *&extent_layer);
  int deserialize_extent_layer(const char *buf, int64_t buf_len, int64_t &pos,
                               common::SequenceNumber &sequence_number,
                               util::autovector<ExtentId> &extent_ids,
                               util::autovector<ExtentId> &lob_extent_ids);
  int calc_extent_stats_unsafe();

  int recycle_lob_extent(bool for_recovery);

  int recycle_unsafe(bool recovery);

  static void async_recycle_meta_snapshot(void *args);
  static int recycle_meta_snapshot(bool for_recovery, util::autovector<ExtentLayerVersion *> &extent_layer_versions);
  static int recycle_extent_layer_versions(bool for_recovery,
                                           util::autovector<ExtentLayerVersion *> &waiting_delete_versions);
  static int recycle_extent_layer_version(ExtentLayerVersion *extent_layer_version, bool for_recovery);
  static int recycle_extent_layer(ExtentLayer *extent_layer, bool for_recovery);
  static int do_recycle_extent(ExtentMeta *extent_meta, bool for_recovery);

private:
  static const int64_t STORAGE_MANAGER_VERSION = 1;

private:
  friend class db::ColumnFamilyData;

  bool is_inited_;
  const db::ColumnFamilyData *owner_;
  const util::EnvOptions &env_options_;
  const common::ImmutableCFOptions &immutable_cfoptions_;
  const common::MutableCFOptions &mutable_cf_options_;
  int32_t column_family_id_;
  int64_t bg_recycle_count_;
  db::InternalKeyComparator *internalkey_comparator_;
  mutable std::mutex meta_mutex_;
  common::SequenceNumber meta_version_;
  ExtentLayerVersion *extent_layer_versions_[MAX_TIER_COUNT];
  db::SnapshotImpl *current_meta_;
  db::SnapshotList meta_snapshot_list_;
  util::autovector<ExtentLayerVersion *> waiting_delete_versions_;
  bool extent_stats_updated_;
  ExtentStats extent_stats_;
  LargeObjectExtentMananger *lob_extent_mgr_;
};

} //namespace storage
} //namespace smartengine

#endif // SMARTENGINE_INCLUDE_STORAGE_MANAGER_H_
