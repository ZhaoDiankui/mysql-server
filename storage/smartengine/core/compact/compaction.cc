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

#include "compact/compaction.h"

#include "compact/new_compaction_iterator.h"
#include "logger/log_module.h"
#include "memory/mod_info.h"
#include "storage/extent_meta_manager.h"
#include "storage/extent_space_manager.h"
#include "table/extent_table_factory.h"
#include "table/extent_writer.h"
#include "util/rate_limiter.h"
#include "util/string_util.h"
#include "util/stop_watch.h"


#define START_PERF_STATS(item) \
  util::StopWatchNano item(context_.cf_options_->env, true);
#define RESTART_PERF_STATS(item) item.Start()
#define RECORD_PERF_STATS(item) \
  stats_.perf_stats_.item += item.ElapsedNanos(false);

namespace smartengine
{
using namespace cache;
using namespace common;
using namespace db;
using namespace memory;
using namespace monitor;
using namespace table;
using namespace util;

namespace common
{
extern CompressionType get_compression_type(const ImmutableCFOptions &ioptions, const int level);
}

namespace storage {
GeneralCompaction::GeneralCompaction(const CompactionContext &context,
                                     const ColumnFamilyDesc &cf,
                                     ArenaAllocator &arena)
    : compression_dict_(),
      context_(context),
      cf_desc_(cf),
      merge_extents_(),
      merge_batch_indexes_(),
      prefetched_extents_(),
      write_extent_opened_(false),
      extent_writer_(nullptr),
      block_buffer_(),
      mini_tables_(),
      change_info_(),
      stats_(),
      input_extents_{0,0,0},
      l2_largest_key_(nullptr),
      delete_percent_(0),
      se_iterators_(nullptr),
      arena_(CharArena::DEFAULT_PAGE_SIZE, ModId::kCompaction),
      stl_alloc_(WrapAllocator(arena_)),
      reader_reps_(stl_alloc_)
{
  //change_info_.task_type_ = context_.task_type_;
  se_iterators_ = static_cast<ExtSEIterator *>(arena_.alloc(sizeof(ExtSEIterator) * RESERVE_MERGE_WAY_SIZE));
  for (int  i = 0; i < RESERVE_MERGE_WAY_SIZE; ++i) {
    new(se_iterators_ + i) ExtSEIterator(context.data_comparator_, context.internal_comparator_);
  }
}

GeneralCompaction::~GeneralCompaction() {
  close_extent();
  cleanup();
}

int GeneralCompaction::add_merge_batch(
    const MetaDescriptorList &extents,
    const size_t start, const size_t end) {
  int ret = Status::kOk;
  if (end > extents.size() || start > extents.size() || start >= end) {
    ret = Status::kInvalidArgument;
    COMPACTION_LOG(WARN, "invalid end or start.", K(start), K(end), K(ret));
  } else {
    BlockPosition pos;
    pos.first = merge_extents_.size();
    ExtentMeta *extent_meta = nullptr;
    for (size_t i = start; i < end && SUCCED(ret); i++) {
      // find the newest schema in current compaction task's extents
      MetaDescriptor meta_dt = extents.at(i);
      if (IS_NULL(extent_meta = ExtentMetaManager::get_instance().get_meta(meta_dt.get_extent_id()))) {
        ret = Status::kErrorUnexpected;
        COMPACTION_LOG(WARN, "failed to get meta", K(ret), K(meta_dt), K(i));
      } else {
        // suppose extent hold the keys memory, no need do deep copy again.
        merge_extents_.emplace_back(meta_dt);
        assert(meta_dt.layer_position_.level_ >= 0);
        assert(meta_dt.layer_position_.level_ < 3 /*StorageManager::MAX_LEVEL*/);
        input_extents_[meta_dt.layer_position_.level_] += 1;
      }
    }
    if (SUCCED(ret)) {
      pos.second = merge_extents_.size();
      merge_batch_indexes_.emplace_back(pos);
      MeasureTime(context_.cf_options_->statistics,
                  NUM_FILES_IN_SINGLE_COMPACTION, merge_extents_.size());
    }
  }
  return ret;
}

int GeneralCompaction::open_extent() {
  int ret = Status::kOk;

  if (!write_extent_opened_) {
    se_assert(nullptr == extent_writer_);
    START_PERF_STATS(create_extent);
    mini_tables_.change_info_ = &change_info_;
    mini_tables_.table_space_id_ = context_.table_space_id_;
    bool is_flush = (TaskType::FLUSH_LEVEL1_TASK == context_.task_type_);
    storage::LayerPosition output_layer_position(context_.output_level_);
    if (0 == context_.output_level_) {
      output_layer_position.layer_index_ = storage::LayerPosition::NEW_GENERATE_LAYER_INDEX;
    } else {
      output_layer_position.layer_index_ = 0;
    }

    CompressionType compression_type = get_compression_type(*(context_.cf_options_), context_.output_level_);
    /**TODO(Zhao Dongsheng): The way of obtaining the block cache is not elegent. */
    ExtentBasedTableFactory *tmp_factory = reinterpret_cast<ExtentBasedTableFactory *>(
        context_.cf_options_->table_factory);
    ExtentWriterArgs writer_args(tmp_factory->table_options().cluster_id,
                                 context_.table_space_id_,
                                 tmp_factory->table_options().block_restart_interval,
                                 context_.cf_options_->env->IsObjectStoreInited() ? storage::OBJECT_EXTENT_SPACE : storage::FILE_EXTENT_SPACE,
                                 cf_desc_.table_schema_,
                                 context_.internal_comparator_,
                                 output_layer_position,
                                 tmp_factory->table_options().block_cache.get(),
                                 context_.cf_options_->row_cache.get(),
                                 compression_type,
                                 &change_info_);
    if (IS_NULL(extent_writer_ = MOD_NEW_OBJECT(ModId::kExtentWriter, ExtentWriter))) {
      ret = Status::kMemoryLimit;
      COMPACTION_LOG(WARN, "fail to allocate memory for ExtentWriter", K(ret));
    } else if (FAILED(extent_writer_->init(writer_args))) {
      COMPACTION_LOG(WARN, "fail to init extent writer", K(ret), K(writer_args));
    } else {
      write_extent_opened_ = true;
    }
    RECORD_PERF_STATS(create_extent);

    if (FAILED(ret)) {
      if (IS_NOTNULL(extent_writer_)) {
        MOD_DELETE_OBJECT(ExtentWriter, extent_writer_);
      }
    }
  }

  return ret;
}

int GeneralCompaction::close_extent(MiniTables *flush_tables)
{
  int ret = Status::kOk;
  std::vector<ExtentInfo> extent_infos;
  
  if (write_extent_opened_) {
    se_assert(nullptr != extent_writer_);
    START_PERF_STATS(finish_extent);
    ret = extent_writer_->finish(&extent_infos);
    RECORD_PERF_STATS(finish_extent);
    if (FAILED(ret)) {
      COMPACTION_LOG(ERROR, "write extent failed", K(ret));
    } else {
      COMPACTION_LOG(INFO, "compaction generate new extent stats",
          "index_id", cf_desc_.column_family_id_, "extent_count", extent_infos.size());

      stats_.record_stats_.merge_output_extents += extent_infos.size();
      for (ExtentInfo &extent_info : extent_infos) {
        stats_.record_stats_.total_output_bytes += extent_info.data_size_;
        stats_.record_stats_.merge_datablocks += extent_info.data_block_count_;
        //TODO(Zhao Dongsheng) : The MiniTables structure will be deleted later. 
        //if (nullptr != flush_tables) {
        //  // for mt_ext task, need preheat table_cache
        //  flush_tables->metas.push_back(meta);
        //}
      }
    }
    MOD_DELETE_OBJECT(ExtentWriter, extent_writer_);
    write_extent_opened_ = false;
    clear_current_writers();
  }

  return ret;
}

void GeneralCompaction::start_record_compaction_stats() {
  assert(nullptr != context_.cf_options_ && nullptr != context_.cf_options_->env);
  if (2 == context_.output_level_) {
    COMPACTION_LOG(INFO, "begin to run major compaction.", K(cf_desc_.column_family_id_));
  } else if (1 == context_.output_level_) {
    COMPACTION_LOG(INFO, "begin to run minor compaction.", K(cf_desc_.column_family_id_));
  } else {
    COMPACTION_LOG(INFO, "begin to run intra_l0 compaction.", K(cf_desc_.column_family_id_));
  }
  stats_.record_stats_.start_micros = context_.cf_options_->env->NowMicros();
}

void GeneralCompaction::stop_record_compaction_stats() {
  assert(nullptr != context_.cf_options_ && nullptr != context_.cf_options_->env);
  stats_.record_stats_.end_micros = context_.cf_options_->env->NowMicros();
  stats_.record_stats_.micros = stats_.record_stats_.end_micros - stats_.record_stats_.start_micros;
  if (stats_.record_stats_.micros == 0) {
    stats_.record_stats_.micros = 1;
  }
  MeasureTime(context_.cf_options_->statistics, COMPACTION_TIME,
              stats_.record_stats_.micros);

  // Write amplification = (not_reused_merge_input_extent) / (not_reused_input_level_extents)
  int64_t input_level_not_reused_extents = 0;
  if (0 != stats_.record_stats_.total_input_extents_at_l0) {
    input_level_not_reused_extents =
        stats_.record_stats_.total_input_extents_at_l0 - stats_.record_stats_.reuse_extents_at_l0;
  } else {
    input_level_not_reused_extents =
        stats_.record_stats_.total_input_extents_at_l1 - stats_.record_stats_.reuse_extents_at_l1;
  }
  stats_.record_stats_.write_amp = input_level_not_reused_extents == 0 ? 0 :
    ((stats_.record_stats_.total_input_extents - stats_.record_stats_.reuse_extents)
     / input_level_not_reused_extents);

  // Calulate rate if rate_limiter is set.
  int64_t old_total_bytes_through = 0;
  if (context_.cf_options_->rate_limiter != nullptr) {
    old_total_bytes_through = context_.cf_options_->rate_limiter->GetTotalBytesThrough();
  }
  char buf[64] = "NONE";
  if (context_.cf_options_->rate_limiter != nullptr) {
    snprintf (buf, 64, "%.2fMB/s",
        (context_.cf_options_->rate_limiter->GetTotalBytesThrough() -
         old_total_bytes_through) * 1000000.0 / stats_.record_stats_.micros
        / 1024 / 1024);
  }
  double merge_rate = (1000000.0 * (stats_.record_stats_.total_input_extents -
      stats_.record_stats_.reuse_extents) * MAX_EXTENT_SIZE /
      1024 / 1024 / stats_.record_stats_.micros);
  Slice merge_ratio(buf, 64);
  COMPACTION_LOG(INFO, "compact ok.",
      K(context_.output_level_),
      K(cf_desc_.column_family_id_),
      "total time", stats_.record_stats_.micros / 1000000,
      "merge rate", merge_rate,
      "merge ratio", merge_ratio.ToString(false).c_str());
}

int GeneralCompaction::down_level_extent(const MetaDescriptor &extent) {
  // move level 1 's extent to level 2;
  int ret = 0;
  COMPACTION_LOG(INFO, "reuse extent", K(extent),
      K(context_.output_level_), K(cf_desc_.column_family_id_));
  ExtentMeta *extent_meta = nullptr;
  LayerPosition layer_position = (0 == context_.output_level_)
                                 ? (LayerPosition(0, storage::LayerPosition::NEW_GENERATE_LAYER_INDEX))
                                 : (LayerPosition(context_.output_level_, 0));

  if (0 != extent.layer_position_.level_
      && (extent.layer_position_.level_ == context_.output_level_)) {
    // no need down
  } else if (FAILED(delete_extent_meta(extent))) {
    COMPACTION_LOG(WARN, "delete extent meta failed.", K(ret), K(extent));
  } else if (IS_NULL(extent_meta = ExtentMetaManager::get_instance().get_meta(extent.extent_id_))) {
    ret = Status::kErrorUnexpected;
    COMPACTION_LOG(WARN, "failed to get meta", K(ret), K(extent.extent_id_));
  } else if (FAILED(change_info_.add_extent(layer_position, extent.extent_id_))) {
    COMPACTION_LOG(WARN, "fail to reuse extent.", K(ret), K(extent));
  } else {
    // stats
    stats_.record_stats_.reuse_extents += 1;
    if (1 == extent.type_.level_) {
      stats_.record_stats_.reuse_extents_at_l1 += 1;
    } else if (0 == extent.type_.level_) {
      stats_.record_stats_.reuse_extents_at_l0 += 1;
    }
  }

  return ret;
}

bool GeneralCompaction::check_do_reuse(const MetaDescriptor &meta) const {
  bool bret = true;
  if (2 == context_.output_level_ && meta.delete_percent_ >= delete_percent_) {
    bret = false;
    COMPACTION_LOG(DEBUG, "REUSE CHECK: not do reuse", K(meta), K(delete_percent_));
  } else {
    COMPACTION_LOG(DEBUG, "REUSE CHECK: do reuse", K(meta), K(delete_percent_));
  }
  return bret;
}

// TODO(Zhao Dongsheng): The reused block need migrate block cache?
int GeneralCompaction::copy_data_block(const MetaDescriptor &data_block_desc)
{
  int ret = Status::kOk;
  char *block_buf = nullptr;
  Slice data_block;
  ExtSEIterator::ReaderRep &reader_rep = reader_reps_[data_block_desc.type_.sequence_];

  if (!write_extent_opened_ && FAILED(open_extent())) {
    COMPACTION_LOG(WARN, "fail to open extent to write", K(ret));
  } else if (IS_NULL(reader_rep.extent_reader_) || IS_NULL(reader_rep.aio_handle_)) {
    ret = Status::kErrorUnexpected;
    COMPACTION_LOG(WARN, "the extent reader and aio handle must not be nullptr", K(ret),
        KP(reader_rep.extent_reader_), KP(reader_rep.aio_handle_), K(data_block_desc));
  } else if (IS_NULL(block_buf = reinterpret_cast<char *>(base_malloc(data_block_desc.block_info_.get_handle().get_size(), ModId::kCompaction)))) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "fail to allocate memory for block buf", K(ret), K(data_block_desc.block_info_.get_handle()));
  } else if (FAILED(reader_rep.extent_reader_->get_data_block(data_block_desc.block_info_.get_handle(), reader_rep.aio_handle_, block_buf, data_block))) {
    COMPACTION_LOG(WARN, "fail to get data block", K(ret), K(data_block_desc));
  } else if (FAILED(extent_writer_->append_block(data_block,
                                                 data_block_desc.value_,
                                                 data_block_desc.range_.end_key_))) {
    COMPACTION_LOG(WARN, "fail to write block", K(ret));
  } else {
    stats_.record_stats_.total_input_bytes += data_block.size();
    stats_.record_stats_.reuse_datablocks += 1;
    if (1 == data_block_desc.type_.level_) {
      stats_.record_stats_.reuse_datablocks_at_l1 += 1;
    } else if (0 == data_block_desc.type_.level_) {
      stats_.record_stats_.reuse_datablocks_at_l0 += 1;
    }
  }
  
  // TODO(Zhao Dongsheng) : The memory of data block can be reused for performance optimization.
  if (IS_NOTNULL(block_buf)) {
    base_free(const_cast<char *>(block_buf));
    block_buf = nullptr;
  }

  return ret;
}

int GeneralCompaction::create_extent_index_iterator(const MetaDescriptor &extent_desc,
                                                    int64_t &iterator_index,
                                                    DataBlockIterator *&iterator,
                                                    ExtSEIterator::ReaderRep &rep)
{
  int ret = Status::kOk;
  PrefetchedExtent *prefetched_extent = nullptr;

  if (IS_NULL(prefetched_extent = get_prefetched_extent(extent_desc.block_position_.first)) &&
      FAILED(prefetch_extent(extent_desc.block_position_.first, prefetched_extent))) {
    COMPACTION_LOG(WARN, "fail to get prefetched extent", K(ret), K(extent_desc));
  } else {
    rep.extent_reader_ = prefetched_extent->reader_;
    rep.aio_handle_ = &(prefetched_extent->aio_handle_);
    if (IS_NULL(rep.index_iterator_ = MOD_NEW_OBJECT(ModId::kCompaction, RowBlockIterator))) {
      ret = Status::kMemoryLimit;
      COMPACTION_LOG(WARN, "fail to allocate memory for index block iterator", K(ret));
    } else if (FAILED(rep.extent_reader_->setup_index_block_iterator(rep.index_iterator_))) {
      COMPACTION_LOG(WARN, "fail to setup index block iterator", K(ret));
    } else {
      iterator_index = reader_reps_.size();
      rep.extent_id_ = extent_desc.block_position_.first;
      MetaType meta_type(MetaType::SSTable,
                         MetaType::DataBlock,
                         MetaType::InternalKey,
                         extent_desc.type_.level_,
                         extent_desc.type_.way_,
                         iterator_index);
      if (IS_NULL(rep.block_iter_ = MOD_NEW_OBJECT(ModId::kCompaction, DataBlockIterator, meta_type, rep.index_iterator_))) {
        ret = Status::kMemoryLimit;
        COMPACTION_LOG(WARN, "fail to allocate memory for data block iterator", K(ret));
      } else {
        stats_.record_stats_.total_input_bytes += rep.extent_reader_->get_data_size();
        iterator = rep.block_iter_;
        reader_reps_.emplace_back(rep);
      }
    }
  }

  // Resource clean
  if (FAILED(ret)) {
    if (IS_NOTNULL(rep.index_iterator_)) {
      MOD_DELETE_OBJECT(RowBlockIterator, rep.index_iterator_);
    }
    if (IS_NOTNULL(rep.block_iter_)) {
      MOD_DELETE_OBJECT(DataBlockIterator, rep.block_iter_);
    }
  }

  return ret;
}

int GeneralCompaction::destroy_extent_index_iterator(const int64_t iterator_index)
{
  int ret = Status::kOk;
  int64_t erase_count = 0;
  PrefetchedExtent *prefetched_extent = nullptr;

  if (iterator_index < (int64_t)reader_reps_.size()) {
    ExtSEIterator::ReaderRep &rep = reader_reps_[iterator_index];
    if (0 != rep.extent_id_) {
      auto iter = prefetched_extents_.find(rep.extent_id_);
      if (prefetched_extents_.end() == iter) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "the prefetched extent must exist", K(ret), "extent_id", ExtentId(rep.extent_id_));
      } else if (IS_NULL(prefetched_extent = iter->second)) {
        ret = Status::kErrorUnexpected;
        SE_LOG(WARN, "the prefetched extent must not be nullptr", K(ret), "extent_id", ExtentId(rep.extent_id_));
      } else {
        MOD_DELETE_OBJECT(ExtentReader, prefetched_extent->reader_);
        MOD_DELETE_OBJECT(PrefetchedExtent, prefetched_extent);
        if (1 != (erase_count = prefetched_extents_.erase(rep.extent_id_))) {
          ret = Status::kErrorUnexpected;
          COMMON_LOG(WARN, "fail to erase prefetched extent", K(ret), 
              K(erase_count), "extent_id", ExtentId(rep.extent_id_));
        }
      }

      if (IS_NOTNULL(rep.index_iterator_)) {
        MOD_DELETE_OBJECT(RowBlockIterator, rep.index_iterator_);
      }

      if (IS_NOTNULL(rep.block_iter_)) {
        MOD_DELETE_OBJECT(DataBlockIterator, rep.block_iter_);
      }

      // reset reader_rep, avoid re-destroy
      rep.extent_id_ = 0;
      stats_.record_stats_.merge_extents += 1;
    }
  }

  return ret;
}

int GeneralCompaction::delete_extent_meta(const MetaDescriptor &extent) {
  int ret = Status::kOk;
  COMPACTION_LOG(INFO, "delete extent", K(cf_desc_.column_family_id_), K(extent), K(context_.output_level_));
  ret = change_info_.delete_extent(extent.layer_position_, extent.extent_id_);
  return ret;
}

int GeneralCompaction::create_data_block_iterator(ExtentReader *extent_reader,
                                                  BlockDataHandle<RowBlock> &block_handle,
                                                  RowBlockIterator *&block_iterator)
{
  int ret = Status::kOk;
  bool in_cache = false;

  if (IS_NULL(extent_reader)) {
    ret = Status::kInvalidArgument;
    COMPACTION_LOG(WARN, "the extent reader must not be nullptr", K(ret));
  } else if (IS_NULL(block_iterator = MOD_NEW_OBJECT(ModId::kCompaction, RowBlockIterator))) {
    ret = Status::kMemoryLimit;
    COMPACTION_LOG(WARN, "fail to allocate memory for data block iterator", K(ret));
  } else if (FAILED(extent_reader->setup_data_block_iterator(false /*use_cache*/, block_handle, block_iterator))) {
    COMPACTION_LOG(WARN, "fail to setup data block iterator", K(ret));
  } else {
    // TODO(Zhao Dongsheng) : Does the cache migration mechanism only apply to data
    // compacted to level 1?
    if (1 == context_.output_level_ && FAILED(extent_reader->check_block_in_cache(block_handle.block_info_.get_handle(), in_cache))) {
      COMPACTION_LOG(WARN, "fail to check block in cache", K(ret), K(block_handle.block_info_));
    } else if (in_cache) {
      // TODO(Zhao Dongsheng): We need to evaluate whether to proactively invalidate the
      // current block cache here.And the extent writer may not be open at this point,
      // which could lead to missing some blocks that should have been migrated.
      if (IS_NOTNULL(extent_writer_)) {
        extent_writer_->set_migrate_flag();
      }
    }
  }

  if (FAILED(ret)) {
    if (IS_NOTNULL(block_iterator)) {
      MOD_DELETE_OBJECT(RowBlockIterator, block_iterator);
    }
  }

  return ret;
}

int GeneralCompaction::destroy_data_block_iterator(table::RowBlockIterator *block_iter)
{
  if (IS_NOTNULL(block_iter)) {
    MOD_DELETE_OBJECT(RowBlockIterator, block_iter);
  }

  return Status::kOk;
}

int GeneralCompaction::build_multiple_seiterators(
    const int64_t batch_index,
    const storage::BlockPosition &batch,
    MultipleSEIterator *&merge_iterator) {
  int ret = 0;
  MultipleSEIterator *multiple_se_iterators =
      ALLOC_OBJECT(MultipleSEIterator, arena_,
      context_.data_comparator_, context_.internal_comparator_);
  if (nullptr == multiple_se_iterators) {
    ret = Status::kMemoryLimit;
    COMPACTION_LOG(WARN, "failed to alloc memory for multiple_se_iterators", K(ret));
  } else {
    for (int64_t index = batch.first; index < batch.second; ++index) {
      const storage::MetaDescriptor &extent = merge_extents_.at(index);
      se_iterators_[extent.type_.way_].add_extent(extent);
      if (1 == extent.layer_position_.level_) {
        stats_.record_stats_.total_input_extents_at_l1 += 1;
      } else if (0 == extent.layer_position_.level_) {
        stats_.record_stats_.total_input_extents_at_l0 += 1;
      }
    }
    for (int i = 0; i < RESERVE_MERGE_WAY_SIZE; ++i) {
      if (se_iterators_[i].has_data()) {
        se_iterators_[i].set_compaction(this);
        multiple_se_iterators->add_se_iterator(&se_iterators_[i]);
      }
    }
    merge_iterator = multiple_se_iterators;
  }
  return ret;
}

int GeneralCompaction::build_compactor(NewCompactionIterator *&compactor,
      MultipleSEIterator *merge_iterator) {
  int ret = 0;
  compactor = ALLOC_OBJECT(NewCompactionIterator, arena_,
      context_.data_comparator_, context_.internal_comparator_,
      kMaxSequenceNumber, &context_.existing_snapshots_,
      context_.earliest_write_conflict_snapshot_,
      true /*do not allow corrupt key*/,
      merge_iterator,
      arena_, change_info_, context_.output_level_/*output level*/,
      context_.shutting_down_, context_.bg_stopped_, context_.cancel_type_, true,
      context_.mutable_cf_options_->background_disable_merge);
  return ret;
}

int GeneralCompaction::merge_extents(MultipleSEIterator *&merge_iterator,
                                     MiniTables *flush_tables) {
  assert(reader_reps_.size() == 0);
  int ret = 0;
  // 1. build compaction_iterator
  NewCompactionIterator *compactor = nullptr;
  if (FAILED(build_compactor(compactor, merge_iterator))) {
    COMPACTION_LOG(WARN, "failed to build compactor", K(ret));
  } else if (nullptr == compactor) {
    ret = Status::kMemoryLimit;
    COMPACTION_LOG(WARN, "create compact iterator error.", K(ret));
  } else if (FAILED(compactor->seek_to_first())) {
    COMPACTION_LOG(WARN, "compactor seek to first failed.", K(ret));
  } else {
  }

  // 2. output the row or reuse extent/block
  while (SUCCED(ret) && compactor->valid() && !context_.bg_stopped_->load()) {
    SEIterator::IterLevel output_level = compactor->get_output_level();
    if (SEIterator::kKVLevel == output_level) {
      if (!write_extent_opened_) {
        if (FAILED(open_extent())) {
          COMPACTION_LOG(WARN, "open extent failed", K(ret));
          break;
        }
      }
      if (FAILED(extent_writer_->append_row(compactor->key(), compactor->value()))) {
        COMPACTION_LOG(WARN, "add kv failed", K(ret), K(compactor->key()));
      } else {
        stats_.record_stats_.merge_output_records += 1;
      }
    } else if (SEIterator::kExtentLevel == output_level) {
      if (FAILED(close_extent(flush_tables))) {
        COMPACTION_LOG(WARN, "close extent failed", K(ret));
      } else { // down l1 to l2 / l0 to l1 / l0 to l0
        ret = down_level_extent(compactor->get_reuse_meta());
      }
    } else if (SEIterator::kBlockLevel == output_level) {
      MetaDescriptor meta = compactor->get_reuse_meta(/*schema*/);
      ret = copy_data_block(meta/*, schema*/);
    } else if (SEIterator::kDataEnd == output_level) {
      ret = Status::kCorruption;
      COMPACTION_LOG(WARN, "output level is invalid.", K(ret));
    } else {
      // has been reused, extent/block
    }
    // get next item
    if (SUCCED(ret) && FAILED(compactor->next())) {
      COMPACTION_LOG(WARN, "compaction iterator get next failed.", K(ret));
    }
  }
  // deal with shutting down
  if (context_.shutting_down_->load(std::memory_order_acquire) ||
     context_.bg_stopped_->load(std::memory_order_acquire)) {
    // Since the next() method will check shutting down, invalid iterator
    // and set status_, we distinguish shutting down and normal ending
    ret = Status::kShutdownInProgress;  // cover ret, by design
    COMPACTION_LOG(WARN, "process shutting down or bg_stop, break compaction.", K(ret));
  }

  if (SUCCED(ret)) {
    record_compaction_iterator_stats(*compactor, stats_.record_stats_);
  }

  FREE_OBJECT(NewCompactionIterator, arena_, compactor);
  FREE_OBJECT(MultipleSEIterator, arena_, merge_iterator);
  clear_current_readers();
  return ret;
}

int GeneralCompaction::run() {
  int ret = Status::kOk;
  start_record_compaction_stats();
  int64_t batch_size = (int64_t)merge_batch_indexes_.size();
  // merge extents start
  for (int64_t index = 0; SUCCED(ret) && index < batch_size; ++index) {
    if (context_.shutting_down_->load(std::memory_order_acquire)) {
      ret = Status::kShutdownInProgress;
      COMPACTION_LOG(WARN, "process shutting down, break compaction.", K(ret));
      break;
    }
    const BlockPosition &batch = merge_batch_indexes_.at(index);
    int64_t extents_size = batch.second - batch.first;
    bool no_reuse = true;
    if (extents_size <= 0) {
      ret = Status::kCorruption;
      COMPACTION_LOG(ERROR, "batch is empty", K(batch.second), K(ret));
    } else if (1 == extents_size) {
      if (batch.first < 0 || batch.first >= (int64_t)merge_extents_.size()) {
        ret = Status::kCorruption;
        COMPACTION_LOG(WARN, "batch index is not valid > extents, BUG here!",
            K(batch.first), K(batch.second), K(merge_extents_.size()));
      } else if (!check_do_reuse(merge_extents_.at(batch.first))) {
        // (1->2)has many delete records or arrange extents' space, not to do reuse
      } else if (FAILED(close_extent())) {
        COMPACTION_LOG(WARN, "failed to close extent,write_opened, mini tables", K(ret),
            K(write_extent_opened_), K(mini_tables_.metas.size()), K(mini_tables_.props.size()));
      } else {
        no_reuse = false;
        const MetaDescriptor &extent = merge_extents_.at(batch.first);
        if (FAILED(down_level_extent(extent))) {
          COMPACTION_LOG(WARN, "down level extent failed", K(ret), K(extent));
        }
      }
    }
    if (SUCCED(ret) && no_reuse) {
      START_PERF_STATS(create_extent);
      RECORD_PERF_STATS(create_extent);
      MultipleSEIterator *merge_iterator = nullptr;
      if (FAILED(build_multiple_seiterators(index, batch, merge_iterator))) {
        COMPACTION_LOG(WARN, "failed to build multiple seiterators",
            K(ret), K(index), K(batch.first), K(batch.second));
      } else if (FAILED(merge_extents(merge_iterator))) {
        COMPACTION_LOG(WARN, "merge extents failed", K(index), K(ret));
      }
    }
    stats_.record_stats_.total_input_extents += extents_size;
  }
  // merge extents end
  if (SUCCED(ret) && FAILED(close_extent())) {
    COMPACTION_LOG(WARN, "close extent failed.", K(ret));
  }
  stop_record_compaction_stats();
  return ret;
}

int GeneralCompaction::prefetch_extent(int64_t id, PrefetchedExtent *&prefetched_extent)
{
  int ret = Status::kOk;
  ExtentId extent_id(id);
  /**TODO(Zhao Dongsheng): The way of obtaining the block cache is not elegent. */
  ExtentBasedTableFactory *tmp_factory = reinterpret_cast<ExtentBasedTableFactory*>(context_.cf_options_->table_factory);
  Cache *block_cache = tmp_factory->table_options().block_cache.get();
  ExtentReaderArgs extent_reader_args(extent_id, context_.internal_comparator_, block_cache);

  if (IS_NULL(prefetched_extent = MOD_NEW_OBJECT(ModId::kCompaction, PrefetchedExtent))) {
    ret = Status::kMemoryLimit;
    SE_LOG(WARN, "fail to allocate memory for PrefetchedExtent", K(ret));
  } else if (IS_NULL(prefetched_extent->reader_ = MOD_NEW_OBJECT(ModId::kExtentReader, ExtentReader))) {
    ret = Status::kMemoryLimit;
    COMPACTION_LOG(WARN, "fail to allocate memory for ExtentReader", K(ret));
  } else if (FAILED(prefetched_extent->reader_->init(extent_reader_args))) {
    COMPACTION_LOG(WARN, "fail to init extent reader", K(ret), K(extent_reader_args));
  } else {
    prefetched_extent->aio_handle_.aio_req_.reset(new AIOReq());
    if (FAILED(prefetched_extent->reader_->do_io_prefetch(0, storage::MAX_EXTENT_SIZE, &(prefetched_extent->aio_handle_)))) {
      COMPACTION_LOG(WARN, "fail to prefetch the extent", K(extent_id));
    } else {
      prefetched_extents_.insert(std::make_pair(id, prefetched_extent));
    }
  }

  // Resource clean
  if (FAILED(ret)) {
    if (IS_NOTNULL(prefetched_extent)) {
      if (IS_NOTNULL(prefetched_extent->reader_)) {
        MOD_DELETE_OBJECT(ExtentReader, prefetched_extent->reader_);
      }
      MOD_DELETE_OBJECT(PrefetchedExtent, prefetched_extent);
    }
  }

  return ret;
}

GeneralCompaction::PrefetchedExtent *GeneralCompaction::get_prefetched_extent(int64_t extent_id) const
{
  PrefetchedExtent *prefetched_extent = nullptr;
  std::unordered_map<int64_t, PrefetchedExtent*>::const_iterator iter =
      prefetched_extents_.find(extent_id);
  if (iter != prefetched_extents_.end()) {
    prefetched_extent = iter->second;
    se_assert(IS_NOTNULL(prefetched_extent));
  }
  return prefetched_extent;
}

int GeneralCompaction::cleanup() {
  l2_largest_key_ = nullptr;
  if (nullptr != se_iterators_) {
    for (int64_t i = 0; i < RESERVE_MERGE_WAY_SIZE; ++i) {
      se_iterators_[i].~ExtSEIterator();
    }
  }
  se_iterators_ = nullptr;
  clear_current_readers();
  clear_current_writers();
  if (write_extent_opened_) {
    se_assert(nullptr != extent_writer_);
    extent_writer_->rollback();
    MOD_DELETE_OBJECT(ExtentWriter, extent_writer_);
    write_extent_opened_ = false;
  }
  // cleanup change_info_
  change_info_.clear();
  return 0;
}

void GeneralCompaction::clear_current_readers() {
  // clear se_iterators
  if (nullptr != se_iterators_) {
    for (int64_t i = 0; i < RESERVE_MERGE_WAY_SIZE; ++i) {
      se_iterators_[i].reset();
    }
  }

  //incase some readers not used while error happens break compaction.
  for (int64_t i = 0; i < (int64_t)reader_reps_.size(); ++i) {
    destroy_extent_index_iterator(i);
  }
  reader_reps_.clear();
  for (auto &item : prefetched_extents_) {
    if (nullptr != item.second) {
      MOD_DELETE_OBJECT(ExtentReader, item.second->reader_);
      MOD_DELETE_OBJECT(PrefetchedExtent, item.second);
    }
  }
  prefetched_extents_.clear();
}

void GeneralCompaction::clear_current_writers() {
  mini_tables_.metas.clear();
  mini_tables_.props.clear();
}

void GeneralCompaction::record_compaction_iterator_stats(
    const NewCompactionIterator &iter, CompactRecordStats &stats) {
  const auto &c_iter_stats = iter.iter_stats();
  stats.merge_input_records += c_iter_stats.num_input_records;
  stats.merge_delete_records = c_iter_stats.num_input_deletion_records;
  stats.merge_corrupt_keys = c_iter_stats.num_input_corrupt_records;
  stats.single_del_fallthru = c_iter_stats.num_single_del_fallthru;
  stats.single_del_mismatch = c_iter_stats.num_single_del_mismatch;
  stats.merge_input_raw_key_bytes += c_iter_stats.total_input_raw_key_bytes;
  stats.merge_input_raw_value_bytes += c_iter_stats.total_input_raw_value_bytes;
  stats.merge_replace_records += c_iter_stats.num_record_drop_hidden;
  stats.merge_expired_records += c_iter_stats.num_record_drop_obsolete;
}

}  // namespace storage
}  // namespace smartengine
