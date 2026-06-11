//  Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "table/sst_file_writer.h"
#include "db/column_family.h"
#include "db/dbformat.h"
#include "storage/storage_common.h"
#include "util/file_reader_writer.h"
#include "util/sync_point.h"
#include "table/extent_table_factory.h"
#include "table/extent_writer.h"

namespace smartengine {
using namespace common;
using namespace util;
using namespace db;

namespace table {
const size_t kFadviseTrigger = 1024 * 1024;  // 1MB

struct SstFileWriter::Rep {
  Rep(const EnvOptions& _env_options,
      const Options& options,
      const Comparator* _user_comparator,
      ColumnFamilyHandle* _cfh,
      bool _invalidate_page_cache)
      : file_writer(),
        extent_writer(),
        env_options(_env_options),
        ioptions(options),
        mutable_cf_options(options),
        internal_comparator(_user_comparator),
        cfh(_cfh),
        invalidate_page_cache(_invalidate_page_cache),
        last_fadvise_size(0) {}

  // TODO(Zhao Dongsheng) : the file_writer is useless?
  std::unique_ptr<WritableFileWriter, memory::ptr_destruct<WritableFileWriter>> file_writer;
  std::unique_ptr<ExtentWriter> extent_writer;
  EnvOptions env_options;
  ImmutableCFOptions ioptions;
  MutableCFOptions mutable_cf_options;
  InternalKeyComparator internal_comparator;
  ExternalSstFileInfo file_info;
  InternalKey ikey;
  ColumnFamilyHandle* cfh;
  // If true, We will give the OS a hint that this file pages is not needed
  // everytime we write 1MB to the file
  bool invalidate_page_cache;
  // the size of the file during the last time we called Fadvise to remove
  // cached pages from page cache.
  uint64_t last_fadvise_size;
};

SstFileWriter::SstFileWriter(const EnvOptions& env_options,
                             const Options& options,
                             const Comparator* user_comparator,
                             ColumnFamilyHandle* column_family,
                             bool invalidate_page_cache,
                             db::MiniTables* mtables,
                             memory::SimpleAllocator *alloc)
    : rep_(MOD_NEW_OBJECT(memory::ModId::kRep,
                          Rep,
                          env_options,
                          options,
                          user_comparator,
                          column_family,
                          invalidate_page_cache)),
      internal_alloc_(false) {

  rep_->file_info.file_size = 0;
  mtables_ = mtables;
  if (nullptr == alloc) {
    alloc_ = MOD_NEW_OBJECT(memory::ModId::kDefaultMod, memory::ArenaAllocator, 8 * 1024);
    internal_alloc_ = true;
  }
}

SstFileWriter::~SstFileWriter()
{
  if (rep_->extent_writer) {
    rep_->extent_writer->rollback();
  }

  MOD_DELETE_OBJECT(Rep, rep_);
  if (internal_alloc_) {
    MOD_DELETE_OBJECT(SimpleAllocator, alloc_);
  }
}

Status SstFileWriter::Open(const std::string& file_path) {
  Rep* r = rep_;
  Status s;
  WritableFile *sst_file = nullptr;
  if (IS_NULL(alloc_)) {
    s = Status(Status::kNotInit);
  } else {
    //  std::unique_ptr<WritableFile> sst_file;
    s = r->ioptions.env->NewWritableFile(file_path, sst_file, r->env_options);
  }
  if (!s.ok()) {
    return s;
  }
  CompressionType compression_type = kNoCompression;
  if (!r->ioptions.compression_per_level.empty()) {
    // Use the compression of the last level if we have per level compression
    compression_type = *(r->ioptions.compression_per_level.rbegin());
  }

  se_assert(nullptr != r->cfh);
  ColumnFamilyData* cfd =nullptr;
  auto column_family = reinterpret_cast<ColumnFamilyHandleImpl*>(r->cfh);
  cfd = column_family->cfd();

  mtables_->table_space_id_ = cfd->get_table_space_id();

  //TODO:yuanfen, temp adapt here
  storage::LayerPosition output_layer_position = (0 == mtables_->level)
                                                 ? (storage::LayerPosition(0, storage::LayerPosition::NEW_GENERATE_LAYER_INDEX))
                                                 : (storage::LayerPosition(mtables_->level, 0));
  r->file_writer.reset(ALLOC_OBJECT(WritableFileWriter, *alloc_, sst_file, r->env_options, nullptr, false/*sst_file use allocator*/));

  //TODO(tec) : If table_factory is using compressed block cache, we will
  // be adding the external sst file blocks into it, which is wasteful.
  /**TODO(Zhao Dongsheng): The way of obtaining the block cache is not elegent. */
  table::ExtentBasedTableFactory *tmp_factory = reinterpret_cast<table::ExtentBasedTableFactory *>(
      r->ioptions.table_factory);
  ExtentWriterArgs writer_args(tmp_factory->table_options().cluster_id,
                               cfd->get_table_space_id(),
                               tmp_factory->table_options().block_restart_interval,
                               r->ioptions.env->IsObjectStoreInited() ? storage::OBJECT_EXTENT_SPACE : storage::FILE_EXTENT_SPACE,
                               cfd->get_table_schema(),
                               &(r->internal_comparator),
                               output_layer_position,
                               tmp_factory->table_options().block_cache.get(),
                               r->ioptions.row_cache.get(),
                               compression_type,
                               mtables_->change_info_);
  r->extent_writer.reset(new ExtentWriter());
  s = r->extent_writer->init(writer_args);

  r->file_info.file_path = file_path;
  r->file_info.file_size = 0;
  r->file_info.num_entries = 0;
  r->file_info.sequence_number = 0;
  r->file_info.version = 2;
  return s;
}

Status SstFileWriter::Add(const Slice& user_key, const Slice& value) {
  Rep* r = rep_;
  if (!r->extent_writer) {
    return Status::InvalidArgument("File is not opened"); 
  }

  if (r->file_info.num_entries == 0) {
    r->file_info.smallest_key.assign(user_key.data(), user_key.size());
  } else {
    if (r->internal_comparator.user_comparator()->Compare(
            user_key, r->file_info.largest_key) <= 0) {
      // Make sure that keys are added in order
      return Status::InvalidArgument("Keys must be added in order");
    }
  }

  // TODO(tec) : For external SST files we could omit the seqno and type.
  r->ikey.Set(user_key, 0 /* Sequence Number */,
              ValueType::kTypeValue /* Put */);
  if(Status::kOk != r->extent_writer->append_row(r->ikey.Encode(), value)){
    return Status(Status::kErrorUnexpected, "SstFileWriter add fail", "");
  }

  // update file info
  r->file_info.num_entries++;
  r->file_info.largest_key.assign(user_key.data(), user_key.size());
  //r->file_info.file_size = r->extent_writer->FileSize();

  InvalidatePageCache(false /* closing */);

  return Status::OK();
}

Status SstFileWriter::Finish(ExternalSstFileInfo* file_info) {
  Rep* r = rep_;
  if (!r->extent_writer) {
    return Status::InvalidArgument("File is not opened");
  }
  if (r->file_info.num_entries == 0) {
    return Status::InvalidArgument("Cannot create sst file with no entries");
  }

  Status s = r->extent_writer->finish(nullptr /*extent_infos*/);
  //r->file_info.file_size = r->extent_writer->FileSize();

  if (s.ok()) {
    s = r->file_writer->Sync(false /**use_fsync*/);
    InvalidatePageCache(true /* closing */);
    if (s.ok()) {
      s = r->file_writer->Close();
    }
  }
  if (!s.ok()) {
    r->ioptions.env->DeleteFile(r->file_info.file_path);
  }

  if (file_info != nullptr) {
    *file_info = r->file_info;
  }

  r->extent_writer.reset();
  return s;
}

void SstFileWriter::InvalidatePageCache(bool closing) {
  Rep* r = rep_;
  if (r->invalidate_page_cache == false) {
    // Fadvise disabled
    return;
  }

  //uint64_t bytes_since_last_fadvise =
  //    r->builder->FileSize() - r->last_fadvise_size;
  //if (bytes_since_last_fadvise > kFadviseTrigger || closing) {
  //  TEST_SYNC_POINT_CALLBACK("SstFileWriter::InvalidatePageCache",
  //                           &(bytes_since_last_fadvise));
  //  // Tell the OS that we dont need this file in page cache
  //  r->file_writer->InvalidateCache(0, 0);
  //  r->last_fadvise_size = r->builder->FileSize();
  //}
}

uint64_t SstFileWriter::FileSize() { return rep_->file_info.file_size; }

}  // namespace table
}  // namespace smartengine
