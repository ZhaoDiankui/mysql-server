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
#ifndef SE_DICT_XE_TABLE_H_
#define SE_DICT_XE_TABLE_H_

#include <unordered_map>
#include "dd/types/table.h"
#include "my_inttypes.h"
#include "dict/se_dict_struct.h"
#include "util/slice.h"

namespace smartengine
{
class SeKeyDef;
struct SeInplaceDdlDictInfo;
class SeDdlManager;

/*
  A table definition. This is an entry in the mapping

    dbname.tablename -> {index_nr, index_nr, ... }

  There is only one SeTableDef object for a given table.
  That's why we keep auto_increment value here, too.
*/

class SeTableDef
{
private:
  void check_if_is_mysql_system_table();

  /* Stores 'dbname.tablename' */
  std::string m_dbname_tablename;

  /* Store the db name, table name, and partition name */
  std::string m_dbname;
  std::string m_tablename;
  std::string m_partition;

  uint64_t m_table_id = dd::INVALID_OBJECT_ID;

  void set_name(const std::string &name);

public:
  SeTableDef(const SeTableDef &) = delete;
  SeTableDef &operator=(const SeTableDef &) = delete;

  explicit SeTableDef(const std::string &name)
      : space_id(0),
        m_key_descr_arr(nullptr),
        m_hidden_pk_val(1),
        m_auto_incr_val(1),
        m_inplace_new_tdef(nullptr),
        m_dict_info(nullptr)
  {
    set_name(name);
  }

  SeTableDef(const char *const name, const size_t &len)
      : space_id(0),
        m_key_descr_arr(nullptr),
        m_hidden_pk_val(1),
        m_auto_incr_val(1),
        m_inplace_new_tdef(nullptr),
        m_dict_info(nullptr)
  {
    set_name(std::string(name, len));
  }

  explicit SeTableDef(const smartengine::common::Slice &slice, const size_t &pos = 0)
      : space_id(0),
        m_key_descr_arr(nullptr),
        m_hidden_pk_val(1),
        m_auto_incr_val(1),
        m_inplace_new_tdef(nullptr),
        m_dict_info(nullptr)
  {
    set_name(std::string(slice.data() + pos, slice.size() - pos));
  }

  ~SeTableDef();
  
  int64_t space_id;
  /* Number of indexes */
  uint m_key_count;

  /* Array of index descriptors */
  std::shared_ptr<SeKeyDef> *m_key_descr_arr;

  std::atomic<longlong> m_hidden_pk_val;
  std::atomic<ulonglong> m_auto_incr_val;

  /* Is this a system table */
  bool m_is_mysql_system_table;

  // save the being added keys, it's set in old table instead of altered table
  // as the old table needs them for update
  std::unordered_map<const SeKeyDef *, Added_key_info> m_added_key;

  // We use raw pointer as the key of m_add_key, so ref the container
  // shared_ptr using this.
  std::list<std::shared_ptr<const SeKeyDef>> m_added_key_ref;

  // we use m_inplace_new_tdef as part of old_tdef, then we can update both
  // new subtables and old subtables.
  SeTableDef* m_inplace_new_tdef;

  // for online-copy-ddl, every new key(pk,sk) has Added_key_info,
  // we can check whether there is duplicate-key error during online phase.
  std::unordered_map<const SeKeyDef*, Added_key_info> m_inplace_new_keys;

  // We use raw pointer as the key of m_add_key, so ref the container
  // shared_ptr using this.
  std::list<std::shared_ptr<const SeKeyDef>> m_inplace_new_keys_ref;

  // used to build new_record during online-copy-ddl
  std::shared_ptr<SeInplaceDdlDictInfo> m_dict_info;

  const std::string &full_tablename() const { return m_dbname_tablename; }
  const std::string &base_dbname() const { return m_dbname; }
  const std::string &base_tablename() const { return m_tablename; }
  const std::string &base_partition() const { return m_partition; }

  int create_added_key(std::shared_ptr<SeKeyDef> added_key,
                       Added_key_info info);
  int clear_added_key();

  int create_new_keys_info(std::shared_ptr<SeKeyDef> added_key, Added_key_info info);

  int clear_new_keys_info();

  int clear_keys_for_ddl();

  uint64_t get_table_id() const { return m_table_id; }
  void set_table_id(uint64_t id) { m_table_id = id; }
  bool init_table_id(SeDdlManager& ddl_manager);

  bool write_dd_table(dd::Table* dd_table) const;
  static bool verify_dd_table(const dd::Table* dd_table, uint32_t &hidden_pk);
};

} //namespace smartengine

#endif
