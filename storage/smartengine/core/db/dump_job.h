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
#pragma once

#include "db/column_family.h"
#include "db/dbformat.h"
#include "db/flush_job.h"
#include "db/job_context.h"
#include "db/internal_stats.h"
#include "memtable/memtable_list.h"

namespace smartengine {

namespace db {

class DumpJob : public BaseFlush {
 public:
  DumpJob(ColumnFamilyData* cfd,
          const common::ImmutableDBOptions& db_options,
          const common::MutableCFOptions& mutable_cf_options,
          common::CompressionType output_compression,
          std::atomic<bool>* shutting_down,
          common::SequenceNumber earliest_write_conflict_snapshot,
          JobContext& job_context,
          std::vector<common::SequenceNumber> &existing_snapshots,
          util::Directory* output_file_directory,
          const util::EnvOptions *env_options,
          MemTable *dump_mem,
          const common::SequenceNumber dump_max_seq,
          memory::ArenaAllocator &arena);
  virtual ~DumpJob() override;
  virtual int run(MiniTables& mtables) override;
  virtual int prepare_flush_task(MiniTables& mtables) override;
  virtual void pick_memtable() override;
  void set_dump_mem(MemTable *mem) {
    dump_mem_ = mem;
  }
  int after_run_dump(MiniTables &mtables, int ret);
 protected:
  virtual table::InternalIterator *create_memtable_iterator(const common::ReadOptions &read_options,
                                                            MemTable *memtable) override;

 private:
  MemTable *dump_mem_;
  common::SequenceNumber dump_max_seq_;
};
}
}
