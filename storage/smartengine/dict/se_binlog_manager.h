/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012,2013 Monty Program Ab

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
#ifndef SE_DICT_XE_BINLOG_MANAGER_H_
#define SE_DICT_XE_BINLOG_MANAGER_H_

#include "my_inttypes.h"
#include "dict/se_index.h"

namespace smartengine
{
class SeDictionaryManager;

namespace db
{
class WriteBatchBase;
} // namespace db

/*
  Writing binlog information into se at commit(),
  and retrieving binlog information at crash recovery.
  commit() and recovery are always executed by at most single client
  at the same time, so concurrency control is not needed.

  Binlog info is stored in se as the following.
   key: BINLOG_INFO_INDEX_NUMBER
   value: packed single row:
     binlog_name_length (2 byte form)
     binlog_name
     binlog_position (4 byte form)
     binlog_gtid_length (2 byte form)
     binlog_gtid
*/
class SeBinlogManager {
public:
  SeBinlogManager(const SeBinlogManager &) = delete;

  SeBinlogManager &operator=(const SeBinlogManager &) = delete;

  SeBinlogManager() = default;

  bool init(SeDictionaryManager *const dict);

  void cleanup();

  void update(const char *const binlog_name,
              const my_off_t binlog_pos,
              const char *const binlog_max_gtid,
              db::WriteBatchBase *const batch);

  bool read(char *const binlog_name,
            my_off_t *const binlog_pos,
            char *const binlog_gtid) const;

  void update_slave_gtid_info(const uint &id,
                              const char *const db,
                              const char *const gtid,
                              db::WriteBatchBase *const write_batch);

private:
  SeDictionaryManager *m_dict = nullptr;

  uchar m_key_buf[SeKeyDef::INDEX_NUMBER_SIZE] = {0};

  common::Slice m_key_slice;

  common::Slice pack_value(uchar *const buf, 
                           const char *const binlog_name,
                           const my_off_t &binlog_pos,
                           const char *const binlog_gtid) const;

  bool unpack_value(const uchar *const value,
                    char *const binlog_name,
                    my_off_t *const binlog_pos,
                    char *const binlog_gtid) const;

  std::atomic<SeTableDef *> m_slave_gtid_info_tbl;
};

} //namespace smartengine

#endif
