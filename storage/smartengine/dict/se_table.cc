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
#include "se_table.h"
#include "dict/se_cf_manager.h"
#include "dict/se_dd_operations.h"
#include "dict/se_ddl_manager.h"
#include "dict/se_index.h"
#include "logger/log_module.h"

namespace smartengine
{

extern SeSubtableManager cf_manager;

SeTableDef::~SeTableDef()
{
  /* Don't free key definitions */
  if (m_key_descr_arr) {
    for (uint i = 0; i < m_key_count; i++) {
      m_key_descr_arr[i] = nullptr;
    }

    delete[] m_key_descr_arr;
    m_key_descr_arr = nullptr;
  }

  m_inplace_new_tdef = nullptr;

  m_dict_info = nullptr;
}

void SeTableDef::check_if_is_mysql_system_table()
{
  static const char *const system_dbs[] = {
      "mysql", "performance_schema", "information_schema",
  };

  m_is_mysql_system_table = false;
  for (uint ii = 0; ii < array_elements(system_dbs); ii++) {
    if (strcmp(m_dbname.c_str(), system_dbs[ii]) == 0) {
      m_is_mysql_system_table = true;
      break;
    }
  }
}

void SeTableDef::set_name(const std::string &name)
{
  int err = HA_EXIT_SUCCESS;

  m_dbname_tablename = name;
  err = se_split_normalized_tablename(name,
                                      &m_dbname,
                                      &m_tablename,
                                      &m_partition);
  assert(err == 0);

  check_if_is_mysql_system_table();
  //make compiler happy
  UNUSED(err);
}

int SeTableDef::clear_keys_for_ddl()
{
  clear_added_key();
  clear_new_keys_info();

  m_inplace_new_tdef = nullptr;

  m_dict_info = nullptr;

  return 0;
}

/* for online build-index */
int SeTableDef::create_added_key(std::shared_ptr<SeKeyDef> added_key, Added_key_info info)
{
  m_added_key.emplace(added_key.get(), info);
  m_added_key_ref.push_back(added_key);
  return 0;
}

int SeTableDef::clear_added_key()
{
  m_added_key.clear();

  for (auto kd : m_added_key_ref) {
    kd = nullptr;
  }

  m_added_key_ref.clear();
  return 0;
}

/* for online inplace rebuild table */
int SeTableDef::create_new_keys_info(
    std::shared_ptr<SeKeyDef> added_key,
    Added_key_info info)
{
  m_inplace_new_keys.emplace(added_key.get(), info);
  m_inplace_new_keys_ref.push_back(added_key);
  return 0;
}

int SeTableDef::clear_new_keys_info()
{
  m_inplace_new_keys.clear();

  for (auto kd : m_inplace_new_keys_ref) {
    kd = nullptr;
  }

  m_inplace_new_keys_ref.clear();

  return 0;
}

bool SeTableDef::verify_dd_table(const dd::Table *dd_table, uint32_t &hidden_pk)
{
  if (nullptr != dd_table) {
    auto table_id = dd_table->se_private_id();
    if (dd::INVALID_OBJECT_ID == table_id) return true;

    // verify all user defined indexes
    for (auto index : dd_table->indexes())
      if (SeKeyDef::verify_dd_index(index, table_id))
        return true;

    uint32_t hidden_pk_id = DD_SUBTABLE_ID_INVALID;
    // verify metadata for hidden primary key created by SE if exists
    auto &p = dd_table->se_private_data();
    if (p.exists(dd_table_key_strings[DD_TABLE_HIDDEN_PK_ID]) &&
        (p.get(dd_table_key_strings[DD_TABLE_HIDDEN_PK_ID], &hidden_pk_id) ||
         SeKeyDef::verify_dd_index_ext(p) ||
         (DD_SUBTABLE_ID_INVALID == hidden_pk_id) ||
         (nullptr == cf_manager.get_cf(hidden_pk_id))))
      return true;

    hidden_pk = hidden_pk_id;
    return false;
  }
  return true;
}

bool SeTableDef::init_table_id(SeDdlManager& ddl_manager)
{
  uint64_t table_id = dd::INVALID_OBJECT_ID;
  if (ddl_manager.get_table_id(table_id) || (dd::INVALID_OBJECT_ID == table_id))
    return true;

  m_table_id = table_id;
  return false;
}

bool SeTableDef::write_dd_table(dd::Table* dd_table) const
{
  assert (nullptr != dd_table);
  assert (dd::INVALID_OBJECT_ID != m_table_id);

  dd_table->set_se_private_id(m_table_id);
  size_t key_no = 0;
  size_t dd_index_size = dd_table->indexes()->size();
  for (auto dd_index : *dd_table->indexes()) {
    if (m_key_descr_arr[key_no]->write_dd_index(dd_index, m_table_id)) {
      HANDLER_LOG(ERROR, "write_dd_index failed", "index_name",
                  dd_index->name().c_str(), K(m_dbname_tablename));
      return true;
    }
    ++key_no;
  }

  if (m_key_count > dd_index_size) {
    // table should have a hidden primary key created by SE
    if (m_key_count != (dd_index_size + 1)) {
      HANDLER_LOG(ERROR, "number of keys mismatch", K(m_dbname_tablename), K(m_key_count), K(dd_index_size));
      return true;
    }

    auto& hidden_pk = m_key_descr_arr[dd_index_size];
    uint32_t hidden_pk_id = hidden_pk->get_index_number();
    if (!hidden_pk->is_hidden_primary_key()) {
      HANDLER_LOG(ERROR, "Invalid primary key definition", K(m_dbname_tablename), K(hidden_pk_id));
      return true;
    // persist subtable_id for hidden primary key created by SE
    } else if (dd_table->se_private_data().set(
                   dd_table_key_strings[DD_TABLE_HIDDEN_PK_ID], hidden_pk_id)) {
      HANDLER_LOG(ERROR, "failed to set hidden_pk_id in se_private_data", K(hidden_pk_id), K(m_dbname_tablename));
      return true;
    } else if (hidden_pk->write_dd_index_ext(dd_table->se_private_data())) {
      HANDLER_LOG(ERROR, "failed to set metadata for hidden pk in se_private_data", K(hidden_pk_id), K(m_dbname_tablename));
      return true;
    }
  }

  for (auto dd_column : *dd_table->columns()) {
    if (dd_column->se_private_data().set(
            dd_index_key_strings[DD_INDEX_TABLE_ID], m_table_id)) {
      HANDLER_LOG(ERROR, "failed to set table id in se_private_data",
                  "column", dd_column->name().c_str(), K(m_dbname_tablename));
      return true;
    }
  }

  return false;
}

} //namespace smartengine
