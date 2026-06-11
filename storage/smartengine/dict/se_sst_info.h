/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2016, Facebook, Inc.

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

#pragma once

#include <atomic>
#include <string>

#include "db/db.h"
#include "table/sst_file_writer.h"

namespace smartengine
{
struct MiniTables;

class SeSstFile {
private:
  SeSstFile(const SeSstFile &p) = delete;
  SeSstFile &operator=(const SeSstFile &p) = delete;

  db::DB *const m_db;
  db::ColumnFamilyHandle *const m_cf;
  const common::DBOptions &m_db_options;
  table::SstFileWriter *m_sst_file_writer;
  const std::string m_name;
  const bool m_tracing;
  db::MiniTables *mtables_;

  std::string generateKey(const std::string &key);
  bool rollbacked;

public:
  SeSstFile(db::DB *const db,
            db::ColumnFamilyHandle *const cf,
            const common::DBOptions &db_options,
            const std::string &name,
            const bool tracing,
            db::MiniTables* mtables = nullptr);
  ~SeSstFile();

  common::Status open();
  common::Status put(const common::Slice &key, const common::Slice &value);
  common::Status commit();
  const std::string get_name() const { return m_name; }
  int rollback();
};

class SeSstInfo {
private:
  SeSstInfo(const SeSstInfo &p) = delete;
  SeSstInfo &operator=(const SeSstInfo &p) = delete;

  db::DB *const m_db;
  db::ColumnFamilyHandle *const m_cf;
  const common::DBOptions &m_db_options;
  uint64_t m_curr_size;
  uint64_t m_max_size;
  uint m_sst_count;
  std::string m_error_msg;
  std::string m_prefix;
  static std::atomic<uint64_t> m_prefix_counter;
  static std::string m_suffix;
#if defined(SE_SST_INFO_USE_THREAD)
  std::queue<SeSstFile *> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_cond;
  std::thread *m_thread;
  bool m_finished;
#endif
  SeSstFile *m_sst_file;
  const bool m_tracing;
  db::MiniTables *mtables_;

  int open_new_sst_file();
  void close_curr_sst_file();
  void set_error_msg(const std::string &sst_file_name, const std::string &msg);

#if defined(SE_SST_INFO_USE_THREAD)
  void run_thread();

  static void thread_fcn(void *object);
#endif

public:
  SeSstInfo(db::DB *const db,
            const std::string &tablename,
            const std::string &indexname,
            db::ColumnFamilyHandle *const cf,
            const common::DBOptions &db_options,
            const bool &tracing,
            db::MiniTables* mtables = nullptr,
            db::MiniTables* mtables_c = nullptr);

  ~SeSstInfo();

  int put(const common::Slice &key, const common::Slice &value);

  int commit();

  // if add index failed
  int rollback();

  const std::string &error_message() const { return m_error_msg; }

  static void init(const db::DB *const db);
};

} //namespace smartengine
