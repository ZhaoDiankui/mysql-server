/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012, Monty Program Ab

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

/* C++ standard header files */
#include <string>
#include <vector>

/* MySQL header files */
#include "monitoring/thread_status.h"
#include "table.h"
#include "mysql/plugin.h"
#include <sql_show.h>
#include "sql/sql_class.h"
#include "sql/dd/dd_schema.h"
#include "storage/perfschema/pfs_visitor.h"

/* smartengine header files */
#include "compact/compaction.h"
#include "memtable/memtablerep.h"
#include "transactions/transaction_db.h"
#include "db/db_impl.h"
#include "db/debug_info.h"
#include "db/internal_stats.h"

/* MyX header files */
#include "./ha_smartengine.h"
#include "./se_cf_manager.h"
#include "./se_utils.h"
#include "se_binlog_manager.h"
#include "se_ddl_manager.h"
#include "se_dict_manager.h"
#include "se_hton.h"
#include "se_table.h"
#include "./se_dd_operations.h"
#include "./se_i_s.h"
#include "dict/se_dict_util.h"
#include "transaction/se_transaction_factory.h"

extern SHOW_VAR status_vars[];
extern struct System_status_var global_status_var;
extern mysql_mutex_t LOCK_status;

namespace smartengine {

#ifdef WITH_XENGINE_COMPATIBLE_MODE
#define SE_IS_PREFIX "XENGINE_"
#else
#define SE_IS_PREFIX "SMARTENGINE_"
#endif

/**
  Define the INFORMATION_SCHEMA (I_S) structures needed by MyX storage
  engine.
*/

#define SE_FIELD_INFO(_name_, _len_, _type_, _flag_)                      \
  { _name_, _len_, _type_, 0, _flag_, nullptr, 0 }

#define SE_FIELD_INFO_END                                                 \
  SE_FIELD_INFO(nullptr, 0, MYSQL_TYPE_NULL, 0)

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_CFSTATS dynamic table
 */
namespace SMARTENGINE_CFSTATS_FIELD {
enum { SUBTABLE_ID = 0, STAT_TYPE, VALUE };
} // namespace SMARTENGINE_CFSTATS_FIELD

static ST_FIELD_INFO se_i_s_cfstats_fields_info[] = {
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("STAT_TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO_END};

static int se_i_s_cfstats_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  bool ret;
  uint64_t val;

  const std::vector<std::pair<const std::string, std::string>> cf_properties = {
      {smartengine::db::DB::Properties::kNumImmutableMemTable,
       "NUM_IMMUTABLE_MEM_TABLE"},
      {smartengine::db::DB::Properties::kMemTableFlushPending,
       "MEM_TABLE_FLUSH_PENDING"},
      {smartengine::db::DB::Properties::kCompactionPending, "COMPACTION_PENDING"},
      {smartengine::db::DB::Properties::kCurSizeActiveMemTable,
       "CUR_SIZE_ACTIVE_MEM_TABLE"},
      {smartengine::db::DB::Properties::kCurSizeAllMemTables,
       "CUR_SIZE_ALL_MEM_TABLES"},
      {smartengine::db::DB::Properties::kNumEntriesActiveMemTable,
       "NUM_ENTRIES_ACTIVE_MEM_TABLE"},
      {smartengine::db::DB::Properties::kNumEntriesImmMemTables,
       "NUM_ENTRIES_IMM_MEM_TABLES"},
      {smartengine::db::DB::Properties::kEstimateTableReadersMem,
       "NON_BLOCK_CACHE_SST_MEM_USAGE"},
      {smartengine::db::DB::Properties::kNumLiveVersions, "NUM_LIVE_VERSIONS"}};

  smartengine::db::DB *const se_db = get_se_db();
  const SeSubtableManager &cf_manager = se_get_cf_manager();
  assert(se_db != nullptr);

  for (const auto &subtable_id : cf_manager.get_subtable_ids()) {
    smartengine::db::ColumnFamilyHandle *cfh;

    cfh = cf_manager.get_cf(subtable_id);
    if (cfh == nullptr)
      continue;

    for (const auto &property : cf_properties) {
      if (!se_db->GetIntProperty(cfh, property.first, &val))
        continue;

      assert(tables != nullptr);

      tables->table->field[SMARTENGINE_CFSTATS_FIELD::SUBTABLE_ID]->store(
          subtable_id, true);
      tables->table->field[SMARTENGINE_CFSTATS_FIELD::STAT_TYPE]->store(
          property.second.c_str(), property.second.size(), system_charset_info);
      tables->table->field[SMARTENGINE_CFSTATS_FIELD::VALUE]->store(val, true);

      ret = my_core::schema_table_store_record(thd, tables->table);

      if (ret)
        DBUG_RETURN(ret);
    }
  }
  DBUG_RETURN(0);
}

static int se_i_s_cfstats_init(void *p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_cfstats_fields_info;
    schema->fill_table = se_i_s_cfstats_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_DBSTATS dynamic table
 */
namespace SMARTENGINE_DBSTATS_FIELD {
enum { STAT_TYPE = 0, VALUE };
} // namespace SMARTENGINE_DBSTATS_FIELD
static const int64_t PC_MAX_IDX = 0;

static ST_FIELD_INFO se_i_s_dbstats_fields_info[] = {
    SE_FIELD_INFO("STAT_TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO_END};

static int se_i_s_dbstats_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  bool ret;
  uint64_t val;

  const std::vector<std::pair<std::string, std::string>> db_properties = {
      {smartengine::db::DB::Properties::kBackgroundErrors, "DB_BACKGROUND_ERRORS"},
      {smartengine::db::DB::Properties::kNumSnapshots, "DB_NUM_SNAPSHOTS"},
      {smartengine::db::DB::Properties::kOldestSnapshotTime,
       "DB_OLDEST_SNAPSHOT_TIME"}};

  smartengine::db::DB *const se_db = get_se_db();
  const smartengine::table::BlockBasedTableOptions &table_options =
      se_get_table_options();

  for (const auto &property : db_properties) {
    if (!se_db->GetIntProperty(property.first, &val))
      continue;

    assert(tables != nullptr);

    tables->table->field[SMARTENGINE_DBSTATS_FIELD::STAT_TYPE]->store(
        property.second.c_str(), property.second.size(), system_charset_info);
    tables->table->field[SMARTENGINE_DBSTATS_FIELD::VALUE]->store(val, true);

    ret = my_core::schema_table_store_record(thd, tables->table);

    if (ret)
      DBUG_RETURN(ret);
  }

  /*
    Currently, this can only show the usage of a block cache allocated
    directly by the handlerton. If the column family config specifies a block
    cache (i.e. the column family option has a parameter such as
    block_based_table_factory={block_cache=1G}), then the block cache is
    allocated within the smartengine::common::GetColumnFamilyOptionsFromString().

    There is no interface to retrieve this block cache, nor fetch the usage
    information from the column family.
   */
  val = (table_options.block_cache ? table_options.block_cache->GetUsage() : 0);
  tables->table->field[SMARTENGINE_DBSTATS_FIELD::STAT_TYPE]->store(
      STRING_WITH_LEN("DB_BLOCK_CACHE_USAGE"), system_charset_info);
  tables->table->field[SMARTENGINE_DBSTATS_FIELD::VALUE]->store(val, true);

  ret = my_core::schema_table_store_record(thd, tables->table);

  DBUG_RETURN(ret);
}

static int se_i_s_dbstats_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_dbstats_fields_info;
    schema->fill_table = se_i_s_dbstats_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_PERF_CONTEXT dynamic table
 */
namespace SMARTENGINE_PERF_CONTEXT_FIELD {
enum { TABLE_SCHEMA = 0, TABLE_NAME, PARTITION_NAME, STAT_TYPE, VALUE };
} // namespace SMARTENGINE_PERF_CONTEXT_FIELD

static ST_FIELD_INFO se_i_s_perf_context_fields_info[] = {
    SE_FIELD_INFO("TABLE_SCHEMA", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("TABLE_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("PARTITION_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING,
                       MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO("STAT_TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO_END};

static int se_i_s_perf_context_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  assert(thd != nullptr);
  assert(tables != nullptr);

  int ret = 0;
  Field **field = tables->table->field;

  const std::vector<std::string> tablenames = se_get_open_table_names();
  for (const auto &it : tablenames) {
    std::string str, dbname, tablename, partname;

    if (se_normalize_tablename(it, &str)) {
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }

    if (se_split_normalized_tablename(str, &dbname, &tablename, &partname)) {
      continue;
    }

    assert(field != nullptr);

    // name string in SeTableDef is from filename which uses my_system_filename
    field[SMARTENGINE_PERF_CONTEXT_FIELD::TABLE_SCHEMA]->store(
        dbname.c_str(), dbname.size(), &my_charset_filename);
    field[SMARTENGINE_PERF_CONTEXT_FIELD::TABLE_NAME]->store(
        tablename.c_str(), tablename.size(), &my_charset_filename);
    if (partname.empty()) {
      field[SMARTENGINE_PERF_CONTEXT_FIELD::PARTITION_NAME]->set_null();
    } else {
      field[SMARTENGINE_PERF_CONTEXT_FIELD::PARTITION_NAME]->set_notnull();
      field[SMARTENGINE_PERF_CONTEXT_FIELD::PARTITION_NAME]->store(
          partname.c_str(), partname.size(), system_charset_info);
    }
  }

  DBUG_RETURN(0);
}

static int se_i_s_perf_context_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_perf_context_fields_info;
    schema->fill_table = se_i_s_perf_context_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_PERF_CONTEXT_GLOBAL dynamic table
 */
namespace SMARTENGINE_PERF_CONTEXT_GLOBAL_FIELD {
enum { STAT_TYPE = 0, VALUE };
} // namespace SMARTENGINE_PERF_CONTEXT_GLOBAL_FIELD

static ST_FIELD_INFO se_i_s_perf_context_global_fields_info[] = {
    SE_FIELD_INFO("STAT_TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO_END};

static int se_i_s_perf_context_global_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  assert(thd != nullptr);
  assert(tables != nullptr);
  DBUG_RETURN(0);
}

static int se_i_s_perf_context_global_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);
  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_perf_context_global_fields_info;
    schema->fill_table = se_i_s_perf_context_global_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_GLOBAL_INFO dynamic table
 */
namespace SMARTENGINE_GLOBAL_INFO_FIELD {
enum { TYPE = 0, NAME, VALUE };
}

static ST_FIELD_INFO se_i_s_global_info_fields_info[] = {
    SE_FIELD_INFO("TYPE", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("NAME", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("VALUE", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO_END};

/*
 * helper function for se_i_s_global_info_fill_table
 * to insert (TYPE, KEY, VALUE) rows into
 * information_schema.se_global_info
 */
static int se_global_info_fill_row(my_core::THD *const thd,
                                    my_core::Table_ref *const tables,
                                    const char *const type,
                                    const char *const name,
                                    const char *const value) {
  assert(thd != nullptr);
  assert(tables != nullptr);
  assert(tables->table != nullptr);
  assert(type != nullptr);
  assert(name != nullptr);
  assert(value != nullptr);

  Field **field = tables->table->field;
  assert(field != nullptr);

  field[SMARTENGINE_GLOBAL_INFO_FIELD::TYPE]->store(type, strlen(type),
                                            system_charset_info);
  field[SMARTENGINE_GLOBAL_INFO_FIELD::NAME]->store(name, strlen(name),
                                            system_charset_info);
  field[SMARTENGINE_GLOBAL_INFO_FIELD::VALUE]->store(value, strlen(value),
                                             system_charset_info);

  return my_core::schema_table_store_record(thd, tables->table);
}

static int se_i_s_global_info_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  assert(thd != nullptr);
  assert(tables != nullptr);

  static const uint32_t INT_BUF_LEN = 21;
  static const uint32_t GTID_BUF_LEN = 60;
  static const uint32_t CF_ID_INDEX_BUF_LEN = 60;

  int ret = 0;

  /* binlog info */
  SeBinlogManager *const blm = se_get_binlog_manager();
  assert(blm != nullptr);

  char file_buf[FN_REFLEN + 1] = {0};
  my_off_t pos = 0;
  char pos_buf[INT_BUF_LEN] = {0};
  char gtid_buf[GTID_BUF_LEN] = {0};

  if (blm->read(file_buf, &pos, gtid_buf)) {
    snprintf(pos_buf, INT_BUF_LEN, "%lu", (uint64_t)pos);
    ret |= se_global_info_fill_row(thd, tables, "BINLOG", "FILE", file_buf);
    ret |= se_global_info_fill_row(thd, tables, "BINLOG", "POS", pos_buf);
    ret |= se_global_info_fill_row(thd, tables, "BINLOG", "GTID", gtid_buf);
  }

  /* max index info */
  const SeDictionaryManager *const dict_manager = se_get_dict_manager();
  assert(dict_manager != nullptr);

  uint32_t max_index_id;
  char max_index_id_buf[INT_BUF_LEN] = {0};

  if (dict_manager->get_max_index_id(&max_index_id)) {
    snprintf(max_index_id_buf, INT_BUF_LEN, "%u", max_index_id);
    ret |= se_global_info_fill_row(thd, tables, "MAX_INDEX_ID", "MAX_INDEX_ID",
                                    max_index_id_buf);
  }

  uint64_t max_table_id;
  if (dict_manager->get_max_table_id(&max_table_id)) {
    snprintf(max_index_id_buf, INT_BUF_LEN, "%lu", max_table_id);
    ret |= se_global_info_fill_row(thd, tables, "MAX_TABLE_ID", "MAX_TABLE_ID",
                                    max_index_id_buf);
  }

#if 0
  /* cf_id -> cf_flags */
  char cf_id_buf[INT_BUF_LEN] = {0};
  char cf_value_buf[FN_REFLEN + 1] = {0};
  const SeSubtableManager &cf_manager = se_get_cf_manager();
  std::unique_ptr<smartengine::db::ColumnFamilyHandle> cf_ptr;
  for (const auto &cf_handle : cf_manager.get_all_cf()) {
    cf_ptr.reset(cf_handle);
    uint flags;
    dict_manager->get_cf_flags(cf_handle->GetID(), &flags);
    snprintf(cf_id_buf, INT_BUF_LEN, "%u", cf_handle->GetID());
    snprintf(cf_value_buf, FN_REFLEN, "%s [%u]", cf_handle->GetName().c_str(),
             flags);
    ret |= se_global_info_fill_row(thd, tables, "CF_FLAGS", cf_id_buf,
                                    cf_value_buf);

    if (ret)
      break;
  }

  /* DDL_DROP_INDEX_ONGOING */
  std::unordered_set<GL_INDEX_ID> gl_index_ids;
  dict_manager->get_ongoing_index_operation(
      &gl_index_ids, SeKeyDef::DDL_DROP_INDEX_ONGOING);
  char cf_id_index_buf[CF_ID_INDEX_BUF_LEN] = {0};
  for (auto gl_index_id : gl_index_ids) {
    snprintf(cf_id_index_buf, CF_ID_INDEX_BUF_LEN, "cf_id:%u,index_id:%u",
             gl_index_id.cf_id, gl_index_id.index_id);
    ret |= se_global_info_fill_row(thd, tables, "DDL_DROP_INDEX_ONGOING",
                                    cf_id_index_buf, "");

    if (ret)
      break;
  }
#endif

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_COMPACTION_STATS dynamic table
 */
static int se_i_s_compact_stats_fill_table(
    my_core::THD *thd, my_core::Table_ref *tables,
    my_core::Item *cond MY_ATTRIBUTE((__unused__))) {
  assert(thd != nullptr);
  assert(tables != nullptr);

  DBUG_ENTER_FUNC();

  int ret = 0;

  smartengine::db::DB *se_db = get_se_db();
  SeSubtableManager &cf_manager = se_get_cf_manager();
  assert(se_db != nullptr);

  for (auto subtable_id : cf_manager.get_subtable_ids()) {
    smartengine::db::ColumnFamilyHandle *cfh;
    /*
       Only the cf name is important. Whether it was generated automatically
       does not matter, so is_automatic is ignored.
    */
    cfh = cf_manager.get_cf(subtable_id);
    if (cfh == nullptr) {
      continue;
    }
    std::map<std::string, double> props;
    bool bool_ret MY_ATTRIBUTE((__unused__));
    bool_ret = se_db->GetMapProperty(cfh, "smartengine.cfstats", &props);
    assert(bool_ret);

    for (auto const &prop_ent : props) {
      std::string prop_name = prop_ent.first;
      double value = prop_ent.second;
      std::size_t del_pos = prop_name.find('.');
      assert(del_pos != std::string::npos);
      std::string level_str = prop_name.substr(0, del_pos);
      std::string type_str = prop_name.substr(del_pos + 1);

      Field **field = tables->table->field;
      assert(field != nullptr);
      field[0]->store(subtable_id, true);
      field[1]->store(level_str.c_str(), level_str.size(), system_charset_info);
      field[2]->store(type_str.c_str(), type_str.size(), system_charset_info);
      field[3]->store(value, true);

      ret |= my_core::schema_table_store_record(thd, tables->table);
      if (ret != 0) {
        DBUG_RETURN(ret);
      }
    }
  }

  DBUG_RETURN(ret);
}

static ST_FIELD_INFO se_i_s_compact_stats_fields_info[] = {
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LEVEL", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("TYPE", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("VALUE", sizeof(double), MYSQL_TYPE_DOUBLE, 0),
    SE_FIELD_INFO_END};

namespace // anonymous namespace = not visible outside this source file
{
struct SeDdlScanner : public Se_tables_scanner {
  my_core::THD *m_thd;
  my_core::TABLE *m_table;

  int add_table(SeTableDef *tdef) override;
};
} // anonymous namespace

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_COMPACTION_TASK dynamic table
 */
namespace SMARTENGINE_COMPACTION_TASK_FIELD {
enum {
  SUBTABLE_ID = 0,
  TYPE,
  COMPACTION_TYPE,
  STAGE,
  MEM_DATA,
  LEVEL0_DATA,
  LEVEL1_DATA,
  LEVEL2_DATA
};
} // namespace SE_MEMTABLE_FIELD


static ST_FIELD_INFO se_i_s_se_compaction_task_fields_info[] = {
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("COMPACTION_TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("STAGE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("MEM_DATA", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LEVEL0_DATA", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LEVEL1_DATA", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LEVEL2_DATA", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO_END};

static int se_i_s_se_compaction_task_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();
  int ret = smartengine::common::Status::kOk;
  smartengine::db::DB *se_db = get_se_db();
  smartengine::util::Env *env = se_db->GetEnv();
  std::vector<smartengine::util::ThreadStatus> thread_list;
  // FIXME no mutex protect
  smartengine::common::Status s = env->GetThreadList(&thread_list);
  for (auto thread : thread_list) {
    tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::SUBTABLE_ID]->store(
          thread.subtable_id_, true);
    if (thread.operation_type == smartengine::util::ThreadStatus::OP_FLUSH) {

      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::TYPE]->store(
          STRING_WITH_LEN("FLUSH"), system_charset_info);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::COMPACTION_TYPE]->store(
          STRING_WITH_LEN("FLUSH_MEMTABLE"), system_charset_info);
      std::string stage_name =
        smartengine::util::ThreadStatus::GetOperationStageName(thread.operation_stage);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::STAGE]->store(
          stage_name.c_str(), stage_name.size(), system_charset_info);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::MEM_DATA]->store(
          thread.op_properties
              [smartengine::util::ThreadStatus::FLUSH_BYTES_MEMTABLES],
          true);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::LEVEL0_DATA]->store(0,
                                                                        true);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::LEVEL1_DATA]->store(0,
                                                                        true);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::LEVEL2_DATA]->store(0,
                                                                        true);
      ret = my_core::schema_table_store_record(thd, tables->table);

    } else if (thread.operation_type ==
               smartengine::util::ThreadStatus::OP_COMPACTION) {
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::TYPE]->store(
          STRING_WITH_LEN("COMPACTION"), system_charset_info);
      std::string compaction_type = smartengine::db::get_task_type_name(
          thread.compaction_type_);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::COMPACTION_TYPE]->store(
          compaction_type.c_str(), compaction_type.size(), system_charset_info);
      std::string stage_name =
        smartengine::util::ThreadStatus::GetOperationStageName(thread.operation_stage);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::STAGE]->store(
          stage_name.c_str(), stage_name.size(), system_charset_info);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::MEM_DATA]->store(0, true);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::LEVEL0_DATA]->store(
          smartengine::storage::MAX_EXTENT_SIZE *
          thread.op_properties[smartengine::util::ThreadStatus::COMPACTION_INPUT_EXTENT_LEVEL0], true);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::LEVEL1_DATA]->store(
          smartengine::storage::MAX_EXTENT_SIZE *
          thread.op_properties[smartengine::util::ThreadStatus::COMPACTION_INPUT_EXTENT_LEVEL1], true);
      tables->table->field[SMARTENGINE_COMPACTION_TASK_FIELD::LEVEL2_DATA]->store(
          smartengine::storage::MAX_EXTENT_SIZE *
          thread.op_properties[smartengine::util::ThreadStatus::COMPACTION_INPUT_EXTENT_LEVEL2], true);
      ret = my_core::schema_table_store_record(thd, tables->table);
    }
  }

  DBUG_RETURN(ret);
}
static int se_i_s_se_compaction_task_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_se_compaction_task_fields_info;
    schema->fill_table = se_i_s_se_compaction_task_fill_table;
  }

  DBUG_RETURN(ret);
}

/* Initialize the information_schema.se_memtable virtual table */
namespace SMARTENGINE_COMPACTION_HISTORY_FIELD {
enum {
  SUBTABLE_ID = 0,
  SEQUENCE,
  TYPE,
  COST_TIME,
  START_TIME,
  END_TIME,
  INPUT_EXTENTS,
  REUSE_EXTENTS,
  INPUT_BLOCKS,
  REUSE_BLOCKS,
  INPUT_RECORDS,
  REUSE_RECORDS,
  OUTPUT_EXTENTS,
  OUTPUT_BLOCKS,
  OUTPUT_RECORDS,
  MERGE_RECORDS,
  DELETE_RECORDS,
  READ_SPEED,
  WRITE_SPEED,
  WRITE_AMP
};
} //namespace SMARTENGINE_COMPACTION_HISTORY_FIELD

static ST_FIELD_INFO se_i_s_se_compaction_history_fields_info[] = {
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("SEQUENCE", sizeof(int32_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("TYPE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("COST_TIME", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("START_TIME", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("END_TIME", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("INPUT_EXTENTS", sizeof(int32_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("REUSE_EXTENTS", sizeof(int32_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("INPUT_BLOCKS", sizeof(int32_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("REUSE_BLOCKS", sizeof(int32_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("INPUT_RECORDS", sizeof(int64_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("REUSE_RECORDS", sizeof(int64_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("OUTPUT_EXTENTS", sizeof(int64_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("OUTPUT_BLOCKS", sizeof(int64_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("OUTPUT_RECORDS", sizeof(int32_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("MERGE_RECORDS", sizeof(int32_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("DELETE_RECORDS", sizeof(int32_t), MYSQL_TYPE_LONGLONG,
                       0),
    SE_FIELD_INFO("READ_SPEED", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("WRITE_SPEED", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("WRITE_AMP", sizeof(double), MYSQL_TYPE_DOUBLE, 0),
    SE_FIELD_INFO_END};

static int se_i_s_se_compaction_history_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();
  int ret = smartengine::common::Status::kOk;
  smartengine::db::DB *se_db = get_se_db();
  std::mutex *compaction_history_mutex = nullptr;
  smartengine::storage::CompactionJobStatsInfo *sum = nullptr;
  std::list<smartengine::storage::CompactionJobStatsInfo*> &compaction_history =
      se_db->get_compaction_history(&compaction_history_mutex, &sum);
  // protect the compaction history list
  std::lock_guard<std::mutex> guard(*compaction_history_mutex);
  // put the sum at first
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::SUBTABLE_ID]->store(
        sum->subtable_id_, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::SEQUENCE]->store(
        sum->sequence_, false);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::TYPE]->store(
        STRING_WITH_LEN("SUM"), system_charset_info);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::COST_TIME]->store(
        sum->stats_.record_stats_.micros, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::START_TIME]->store(
        sum->stats_.record_stats_.start_micros, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::END_TIME]->store(
        sum->stats_.record_stats_.end_micros, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::INPUT_EXTENTS]->store(
        sum->stats_.record_stats_.total_input_extents, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::REUSE_EXTENTS]->store(
        sum->stats_.record_stats_.reuse_extents, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::INPUT_BLOCKS]->store(
        0, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::REUSE_BLOCKS]->store(
        sum->stats_.record_stats_.reuse_datablocks, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::INPUT_RECORDS]->store(
        sum->stats_.record_stats_.merge_input_records, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::REUSE_RECORDS]->store(
        0, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::OUTPUT_EXTENTS]->store(
        sum->stats_.record_stats_.merge_output_extents, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::OUTPUT_BLOCKS]->store(
        sum->stats_.record_stats_.merge_datablocks, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::OUTPUT_RECORDS]->store(
        sum->stats_.record_stats_.merge_output_records, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::MERGE_RECORDS]->store(
        0, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::DELETE_RECORDS]->store(
        sum->stats_.record_stats_.merge_delete_records, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::READ_SPEED]->store(0,
                                                                        true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::WRITE_SPEED]->store(
        0, true);
  tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::WRITE_AMP]->store(
        sum->stats_.record_stats_.write_amp, true);
  ret = my_core::schema_table_store_record(thd, tables->table);
  for (auto jobinfo : compaction_history) {
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::SUBTABLE_ID]->store(
        jobinfo->subtable_id_, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::SEQUENCE]->store(
        jobinfo->sequence_, false);

    std::string compaction_type = smartengine::db::get_task_type_name(jobinfo->type_);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::TYPE]->store(
        compaction_type.c_str(), compaction_type.size(), system_charset_info);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::COST_TIME]->store(
        jobinfo->stats_.record_stats_.micros, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::START_TIME]->store(
        jobinfo->stats_.record_stats_.start_micros, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::END_TIME]->store(
        jobinfo->stats_.record_stats_.end_micros, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::INPUT_EXTENTS]->store(
        jobinfo->stats_.record_stats_.total_input_extents, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::REUSE_EXTENTS]->store(
        jobinfo->stats_.record_stats_.reuse_extents, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::INPUT_BLOCKS]->store(
        0, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::REUSE_BLOCKS]->store(
        jobinfo->stats_.record_stats_.reuse_datablocks, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::INPUT_RECORDS]->store(
        jobinfo->stats_.record_stats_.merge_input_records, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::REUSE_RECORDS]->store(
        0, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::OUTPUT_EXTENTS]->store(
        jobinfo->stats_.record_stats_.merge_output_extents, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::OUTPUT_BLOCKS]->store(
        jobinfo->stats_.record_stats_.merge_datablocks, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::OUTPUT_RECORDS]->store(
        jobinfo->stats_.record_stats_.merge_output_records, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::MERGE_RECORDS]->store(
        0, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::DELETE_RECORDS]->store(
        jobinfo->stats_.record_stats_.merge_delete_records, true);

    // Omit the speed when task duration is less than 1 second
    int64_t read_speed = jobinfo->stats_.record_stats_.micros < 1000000 ? 0
      : (1000000 * 2 * jobinfo->stats_.record_stats_.total_input_extents /
          jobinfo->stats_.record_stats_.micros);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::READ_SPEED]->store(
        read_speed, true);

    int64_t merge_speed = jobinfo->stats_.record_stats_.micros < 1000000 ? 0
      : (1000000 * 2 * (jobinfo->stats_.record_stats_.total_input_extents -
            jobinfo->stats_.record_stats_.reuse_extents) /
          jobinfo->stats_.record_stats_.micros);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::WRITE_SPEED]->store(
        merge_speed, true);
    tables->table->field[SMARTENGINE_COMPACTION_HISTORY_FIELD::WRITE_AMP]->store(
        jobinfo->stats_.record_stats_.write_amp, true);
    ret = my_core::schema_table_store_record(thd, tables->table);
  }
  DBUG_RETURN(ret);
}

static int se_i_s_se_compaction_history_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_se_compaction_history_fields_info;
    schema->fill_table = se_i_s_se_compaction_history_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_MEM_ALLOC dynamic table
 */
namespace SMARTENGINE_MEM_ALLOC_FIELD {
enum { MODULE = 0, MALLOC, ALLOC, USED, AVG_ALLOC, ALLOC_CNT, TOTAL_ALLOC_CNT };
} // namespace SMARTENGINE_MEM_ALLOC_FIELD

static ST_FIELD_INFO se_i_s_se_mem_alloc_fields_info[] = {
    SE_FIELD_INFO("MODULE", NAME_LEN + 1, MYSQL_TYPE_STRING, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("MALLOC", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("ALLOC", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("USED", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("AVG_ALLOC", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("ALLOC_CNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("TOTAL_ALLOC_CNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO_END};

static int se_i_s_se_mem_alloc_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();
  int ret = smartengine::common::Status::kOk;
  smartengine::memory::MemItemDump items[smartengine::memory::ModMemSet::kModMaxCnt];
  std::string mod_names[smartengine::memory::ModMemSet::kModMaxCnt];
  smartengine::memory::AllocMgr::get_instance()->get_memory_usage(items, mod_names);
  for (size_t i = 0; i < smartengine::memory::ModMemSet::kModMaxCnt; i++) {
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::MODULE]->store(
        mod_names[i].c_str(), mod_names[i].size(), system_charset_info);
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::MALLOC]->store(
        items[i].malloc_size_, true);
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::ALLOC]->store(items[i].alloc_size_,
                                                            true);
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::USED]->store(
        items[i].hold_size_, true);
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::AVG_ALLOC]->store(
        items[i].avg_alloc_, true);
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::ALLOC_CNT]->store(
        items[i].alloc_cnt_, true);
    tables->table->field[SMARTENGINE_MEM_ALLOC_FIELD::TOTAL_ALLOC_CNT]->store(
        items[i].total_alloc_cnt_, true);
    /* Tell MySQL about this row in the virtual table */
    ret = my_core::schema_table_store_record(thd, tables->table);
    if (ret != 0) {
      break;
    }
  }
  DBUG_RETURN(ret);
}

/* Initialize the information_schema.se_mem_alloc virtual table */
static int se_i_s_se_mem_alloc_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_se_mem_alloc_fields_info;
    schema->fill_table = se_i_s_se_mem_alloc_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_DDL dynamic table
 */
namespace SMARTENGINE_DDL_FIELD {
enum {
  TABLE_SCHEMA = 0,
  TABLE_NAME,
  PARTITION_NAME,
  INDEX_NAME,
  SUBTABLE_ID,
  INDEX_NUMBER,
  INDEX_TYPE,
  KV_FORMAT_VERSION,
  //CF
};
} // namespace SMARTENGINE_DDL_FIELD

static ST_FIELD_INFO se_i_s_ddl_fields_info[] = {
    SE_FIELD_INFO("TABLE_SCHEMA", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("TABLE_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("PARTITION_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING,
                       MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO("INDEX_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("INDEX_NUMBER", sizeof(uint32_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("INDEX_TYPE", sizeof(uint16_t), MYSQL_TYPE_SHORT, 0),
    SE_FIELD_INFO("KV_FORMAT_VERSION", sizeof(uint16_t), MYSQL_TYPE_SHORT,
                       0),
    //SE_FIELD_INFO("CF", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO_END};

int SeDdlScanner::add_table(SeTableDef *tdef) {
  assert(tdef != nullptr);

  int ret = 0;

  assert(m_table != nullptr);
  Field **field = m_table->field;
  assert(field != nullptr);

  // name string in SeTableDef is from filename which uses my_system_filename
  const std::string &dbname = tdef->base_dbname();
  field[SMARTENGINE_DDL_FIELD::TABLE_SCHEMA]->store(dbname.c_str(), dbname.size(),
                                            &my_charset_filename);

  const std::string &tablename = tdef->base_tablename();
  field[SMARTENGINE_DDL_FIELD::TABLE_NAME]->store(tablename.c_str(), tablename.size(),
      strstr(tablename.c_str(), tmp_file_prefix) != NULL ? system_charset_info
                                                         : &my_charset_filename);

  const std::string &partname = tdef->base_partition();
  if (partname.empty()) {
    field[SMARTENGINE_DDL_FIELD::PARTITION_NAME]->set_null();
  } else {
    field[SMARTENGINE_DDL_FIELD::PARTITION_NAME]->set_notnull();
    field[SMARTENGINE_DDL_FIELD::PARTITION_NAME]->store(
        partname.c_str(), partname.size(), &my_charset_filename);

  }

  for (uint i = 0; i < tdef->m_key_count; i++) {
    const SeKeyDef &kd = *tdef->m_key_descr_arr[i];

    field[SMARTENGINE_DDL_FIELD::INDEX_NAME]->store(kd.m_name.c_str(), kd.m_name.size(),
                                            system_charset_info);

    GL_INDEX_ID gl_index_id = kd.get_gl_index_id();
    field[SMARTENGINE_DDL_FIELD::SUBTABLE_ID]->store(gl_index_id.cf_id, true);
    field[SMARTENGINE_DDL_FIELD::INDEX_NUMBER]->store(gl_index_id.index_id, true);
    field[SMARTENGINE_DDL_FIELD::INDEX_TYPE]->store(kd.m_index_type, true);
    field[SMARTENGINE_DDL_FIELD::KV_FORMAT_VERSION]->store(kd.m_kv_format_version,
                                                   true);

    ret = my_core::schema_table_store_record(m_thd, m_table);
    if (ret)
      return ret;
  }
  return HA_EXIT_SUCCESS;
}

static int se_i_s_ddl_fill_table(my_core::THD *const thd,
                                  my_core::Table_ref *const tables,
                                  my_core::Item *const cond) {
  DBUG_ENTER_FUNC();

  assert(thd != nullptr);
  assert(tables != nullptr);

  SeDdlScanner ddl_arg;
  ddl_arg.m_thd = thd;
  ddl_arg.m_table = tables->table;

  SeDdlManager *ddl_manager = se_get_ddl_manager();
  assert(ddl_manager != nullptr);
  int ret = ddl_manager->scan_for_tables(&ddl_arg);

  DBUG_RETURN(ret);
}

static int se_i_s_ddl_init(void *const p) {
  DBUG_ENTER_FUNC();

  my_core::ST_SCHEMA_TABLE *schema;

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_ddl_fields_info;
    schema->fill_table = se_i_s_ddl_fill_table;
  }

  DBUG_RETURN(ret);
}

static int se_i_s_global_info_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);
  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = reinterpret_cast<my_core::ST_SCHEMA_TABLE *>(p);

    schema->fields_info = se_i_s_global_info_fields_info;
    schema->fill_table = se_i_s_global_info_fill_table;
  }

  DBUG_RETURN(ret);
}

static int se_i_s_compact_stats_init(void *p) {
  my_core::ST_SCHEMA_TABLE *schema;

  DBUG_ENTER_FUNC();
  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    schema = reinterpret_cast<my_core::ST_SCHEMA_TABLE *>(p);

    schema->fields_info = se_i_s_compact_stats_fields_info;
    schema->fill_table = se_i_s_compact_stats_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_LOCKS dynamic table
 */
namespace SMARTENGINE_LOCKS_FIELD {
enum { SUBTABLE_ID = 0, TRANSACTION_ID, KEY, MODE };
} // namespace SMARTENGINE_LOCKS_FIELD

static ST_FIELD_INFO se_i_s_lock_info_fields_info[] = {
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO("TRANSACTION_ID", sizeof(uint32_t), MYSQL_TYPE_LONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO("KEY", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("MODE", 32, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO_END};

/* Fill the information_schema.se_locks virtual table */
static int se_i_s_lock_info_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  assert(thd != nullptr);
  assert(tables != nullptr);
  assert(tables->table != nullptr);

  int ret = 0;

  smartengine::util::TransactionDB *const se_db = get_se_db();
  assert(se_db != nullptr);

  /* cf id -> smartengine::util::KeyLockInfo */
  std::unordered_multimap<uint32_t, smartengine::util::KeyLockInfo> lock_info =
      se_db->GetLockStatusData();

  for (const auto &lock : lock_info) {
    const uint32_t cf_id = lock.first;
    const auto &key_lock_info = lock.second;
    const auto key_hexstr = se_hexdump(key_lock_info.key.c_str(),
                                        key_lock_info.key.length(), FN_REFLEN);

    for (const auto &id : key_lock_info.ids) {
      tables->table->field[SMARTENGINE_LOCKS_FIELD::SUBTABLE_ID]->store(cf_id, true);
      tables->table->field[SMARTENGINE_LOCKS_FIELD::TRANSACTION_ID]->store(id, true);

      tables->table->field[SMARTENGINE_LOCKS_FIELD::KEY]->store(
          key_hexstr.c_str(), key_hexstr.size(), system_charset_info);
      tables->table->field[SMARTENGINE_LOCKS_FIELD::MODE]->store(
          key_lock_info.exclusive ? "X" : "S", 1, system_charset_info);

      /* Tell MySQL about this row in the virtual table */
      ret = my_core::schema_table_store_record(thd, tables->table);
      if (ret != 0) {
        break;
      }
    }
  }
  DBUG_RETURN(ret);
}

/* Initialize the information_schema.se_lock_info virtual table */
static int se_i_s_lock_info_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_lock_info_fields_info;
    schema->fill_table = se_i_s_lock_info_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_TRX dynamic table
 */
namespace SMARTENGINE_TRX_FIELD {
enum {
  TRANSACTION_ID = 0,
  STATE,
  NAME,
  WRITE_COUNT,
  LOCK_COUNT,
  TIMEOUT_SEC,
  WAITING_KEY,
  WAITING_SUBTABLE_ID,
  IS_REPLICATION,
  SKIP_TRX_API,
  READ_ONLY,
  HAS_DEADLOCK_DETECTION,
  NUM_ONGOING_BULKLOAD,
  THREAD_ID,
  QUERY
};
} // namespace SMARTENGINE_TRX_FIELD

static ST_FIELD_INFO se_i_s_trx_info_fields_info[] = {
    SE_FIELD_INFO("TRANSACTION_ID", sizeof(ulonglong), MYSQL_TYPE_LONGLONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO("STATE", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("WRITE_COUNT", sizeof(ulonglong), MYSQL_TYPE_LONGLONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LOCK_COUNT", sizeof(ulonglong), MYSQL_TYPE_LONGLONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO("TIMEOUT_SEC", sizeof(uint32_t), MYSQL_TYPE_LONG, 0),
    SE_FIELD_INFO("WAITING_KEY", FN_REFLEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("WAITING_SUBTABLE_ID", sizeof(uint32_t), MYSQL_TYPE_LONG,
                       MY_I_S_UNSIGNED),
    SE_FIELD_INFO("IS_REPLICATION", sizeof(uint32_t), MYSQL_TYPE_LONG, 0),
    SE_FIELD_INFO("SKIP_TRX_API", sizeof(uint32_t), MYSQL_TYPE_LONG, 0),
    SE_FIELD_INFO("READ_ONLY", sizeof(uint32_t), MYSQL_TYPE_LONG, 0),
    SE_FIELD_INFO("HAS_DEADLOCK_DETECTION", sizeof(uint32_t),
                       MYSQL_TYPE_LONG, 0),
    SE_FIELD_INFO("NUM_ONGOING_BULKLOAD", sizeof(uint32_t),
                       MYSQL_TYPE_LONG, 0),
    SE_FIELD_INFO("THREAD_ID", sizeof(ulong), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("QUERY", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO_END};

/* Fill the information_schema.SMARTENGINE_TRX virtual table */
static int se_i_s_trx_info_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  assert(thd != nullptr);
  assert(tables != nullptr);
  assert(tables->table != nullptr);

  int ret = 0;

  const std::vector<SeTrxInfo> &all_trx_info = se_get_all_trx_info();

  for (const auto &info : all_trx_info) {
    auto name_hexstr =
        se_hexdump(info.name.c_str(), info.name.length(), NAME_LEN);
    auto key_hexstr = se_hexdump(info.waiting_key.c_str(),
                                  info.waiting_key.length(), FN_REFLEN);
    tables->table->field[SMARTENGINE_TRX_FIELD::TRANSACTION_ID]->store(info.trx_id,
                                                               true);
    tables->table->field[SMARTENGINE_TRX_FIELD::STATE]->store(
        info.state.c_str(), info.state.length(), system_charset_info);
    tables->table->field[SMARTENGINE_TRX_FIELD::NAME]->store(
        name_hexstr.c_str(), name_hexstr.length(), system_charset_info);
    tables->table->field[SMARTENGINE_TRX_FIELD::WRITE_COUNT]->store(info.write_count,
                                                            true);
    tables->table->field[SMARTENGINE_TRX_FIELD::LOCK_COUNT]->store(info.lock_count,
                                                           true);
    tables->table->field[SMARTENGINE_TRX_FIELD::TIMEOUT_SEC]->store(info.timeout_sec,
                                                            false);
    tables->table->field[SMARTENGINE_TRX_FIELD::WAITING_KEY]->store(
        key_hexstr.c_str(), key_hexstr.length(), system_charset_info);
    tables->table->field[SMARTENGINE_TRX_FIELD::WAITING_SUBTABLE_ID]->store(
        info.waiting_cf_id, true);
    tables->table->field[SMARTENGINE_TRX_FIELD::IS_REPLICATION]->store(
        info.is_replication, false);
    tables->table->field[SMARTENGINE_TRX_FIELD::SKIP_TRX_API]->store(info.skip_trx_api,
                                                             false);
    tables->table->field[SMARTENGINE_TRX_FIELD::READ_ONLY]->store(info.read_only,
                                                          false);
    tables->table->field[SMARTENGINE_TRX_FIELD::HAS_DEADLOCK_DETECTION]->store(
        info.deadlock_detect, false);
    tables->table->field[SMARTENGINE_TRX_FIELD::NUM_ONGOING_BULKLOAD]->store(
        info.num_ongoing_bulk_load, false);
    tables->table->field[SMARTENGINE_TRX_FIELD::THREAD_ID]->store(info.thread_id, true);
    tables->table->field[SMARTENGINE_TRX_FIELD::QUERY]->store(
        info.query_str.c_str(), info.query_str.length(), system_charset_info);

    /* Tell MySQL about this row in the virtual table */
    ret = my_core::schema_table_store_record(thd, tables->table);
    if (ret != 0) {
      break;
    }
  }

  DBUG_RETURN(ret);
}

/* Initialize the information_schema.se_trx_info virtual table */
static int se_i_s_trx_info_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;

    schema = (my_core::ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_i_s_trx_info_fields_info;
    schema->fill_table = se_i_s_trx_info_fill_table;
  }

  DBUG_RETURN(ret);
}

static int se_i_s_deinit(void *p MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();
  DBUG_RETURN(0);
}

namespace SMARTENGINE_TABLES_FIELD {
enum {
  SCHEMA_NAME = 0,
  NAME,
  N_COLS,
  INSTANT_COLS
};
}  // namespace SMARTENGINE_TABLES_FIELD

static ST_FIELD_INFO se_tables_fields_info[] = {
    SE_FIELD_INFO("SCHEMA_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("N_COLS", sizeof(int32_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("INSTANT_COLS", sizeof(int32_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO_END};

static int i_s_se_tables_fill_table(THD *thd, Table_ref *tables, Item *) {
  DBUG_ENTER_FUNC();
  TABLE *table = tables->table;
  bool error = SeDdHelper::traverse_all_se_tables(
      thd, false, 0,
      [&thd, &table](const dd::Schema *dd_schema,
                     const dd::Table *dd_table) -> bool {
        assert(dd_schema && dd_table);
        table->field[SMARTENGINE_TABLES_FIELD::SCHEMA_NAME]->store(
            dd_schema->name().c_str(), dd_schema->name().length(),
            system_charset_info);
        table->field[SMARTENGINE_TABLES_FIELD::NAME]->store(
            dd_table->name().c_str(), dd_table->name().length(),
            system_charset_info);
        table->field[SMARTENGINE_TABLES_FIELD::N_COLS]->store(
            dd_table->columns().size(), true);
        uint32_t instant_cols = 0;
        if (dd_table->se_private_data().exists(
                smartengine::dd_table_key_strings[smartengine::DD_TABLE_INSTANT_COLS])) {
          dd_table->se_private_data().get(
              smartengine::dd_table_key_strings[smartengine::DD_TABLE_INSTANT_COLS],
              &instant_cols);
        }
        table->field[SMARTENGINE_TABLES_FIELD::INSTANT_COLS]->store(instant_cols,
                                                                true);
        return my_core::schema_table_store_record(thd, table);
      });

  if (error) {
    HANDLER_LOG(ERROR, "SE: failed to scan all se tables from data dictionary!");
  }

  DBUG_RETURN(0);
}

/** Bind the dynamic table INFORMATION_SCHEMA.se_tables
@param[in,out]	p	table schema object
@return 0 on success */
static int se_tables_init(void *p) {
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("se_tables_init");

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    schema = (ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_tables_fields_info;
    schema->fill_table = i_s_se_tables_fill_table;
  }

  DBUG_RETURN(ret);
}

/**  SMARTENGINE_COLUMNS  **************************************************/
/* Fields of the dynamic table INFORMATION_SCHEMA.SMARTENGINE_COLUMNS
Every time any column gets changed, added or removed, please remember
to change i_s_innodb_plugin_version_postfix accordingly, so that
the change can be propagated to server */

namespace SMARTENGINE_COLUMNS_FIELD {
enum {
  NAME = 0,
  SCHEMA_NAME,
  TABLE_NAME,
  IS_INSTANTLY_ADDED,
  HAS_ORIGINAL_DEFAULT,
  ORIGIN_DEFAULT_VALUE
};
} // namespace SMARTENGINE_TABLES_FIELD 

static ST_FIELD_INFO se_columns_fields_info[] = {
    SE_FIELD_INFO("NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("SCHEMA_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("TABLE_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("IS_INSTANTLY_ADDED", sizeof(int8_t), MYSQL_TYPE_TINY, 0),
    SE_FIELD_INFO("HAS_ORIGINAL_DEFAULT", sizeof(int8_t), MYSQL_TYPE_TINY,0),
    SE_FIELD_INFO("ORIGIN_DEFAULT_VALUE", 65536 * 4, MYSQL_TYPE_BLOB, MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO_END};

/** Function to fill information_schema.se_columns.
@param[in]	thd		thread
@param[in,out]	tables		tables to fill
@return 0 on success */
static int i_s_se_columns_fill_table(THD *thd, Table_ref *tables, Item *) {
  DBUG_ENTER_FUNC();
  TABLE *table = tables->table;
  bool error = SeDdHelper::traverse_all_se_tables(
      thd, false, 0,
      [&thd, &table](const dd::Schema *dd_schema,
                     const dd::Table *dd_table) -> bool {
        bool res = false;
        for (const auto col : dd_table->columns()) {
          table->field[SMARTENGINE_COLUMNS_FIELD::NAME]->store(
              col->name().c_str(), col->name().length(), system_charset_info);
          table->field[SMARTENGINE_COLUMNS_FIELD::SCHEMA_NAME]->store(
              dd_schema->name().c_str(), dd_schema->name().length(),
              system_charset_info);
          table->field[SMARTENGINE_COLUMNS_FIELD::TABLE_NAME]->store(
              dd_table->name().c_str(), dd_table->name().length(),
              system_charset_info);

          if (col->se_private_data().exists(
                  smartengine::dd_column_key_strings
                      [smartengine::DD_INSTANT_COLUMN_DEFAULT_NULL])) {
            table->field[SMARTENGINE_COLUMNS_FIELD::IS_INSTANTLY_ADDED]->store(
                true, true);
            table->field[SMARTENGINE_COLUMNS_FIELD::HAS_ORIGINAL_DEFAULT]->store(
                false, true);
            table->field[SMARTENGINE_COLUMNS_FIELD::ORIGIN_DEFAULT_VALUE]
                ->set_null();
          } else if (col->se_private_data().exists(
                         smartengine::dd_column_key_strings
                             [smartengine::DD_INSTANT_COLUMN_DEFAULT])) {
            dd::String_type value;
            col->se_private_data().get(
                smartengine::dd_column_key_strings[smartengine::DD_INSTANT_COLUMN_DEFAULT],
                &value);
            table->field[SMARTENGINE_COLUMNS_FIELD::IS_INSTANTLY_ADDED]->store(
                true, true);
            table->field[SMARTENGINE_COLUMNS_FIELD::HAS_ORIGINAL_DEFAULT]->store(
                true, true);
            table->field[SMARTENGINE_COLUMNS_FIELD::ORIGIN_DEFAULT_VALUE]
                ->set_notnull();
            table->field[SMARTENGINE_COLUMNS_FIELD::ORIGIN_DEFAULT_VALUE]->store(
                value.c_str(), value.length(), system_charset_info);
          } else {
            table->field[SMARTENGINE_COLUMNS_FIELD::IS_INSTANTLY_ADDED]->store(
                false, true);
            table->field[SMARTENGINE_COLUMNS_FIELD::HAS_ORIGINAL_DEFAULT]->store(
                false, true);
            table->field[SMARTENGINE_COLUMNS_FIELD::ORIGIN_DEFAULT_VALUE]
                ->set_null();
          }
          res |= my_core::schema_table_store_record(thd, table);
        }
        return res;
      });

  if (error) {
    HANDLER_LOG(ERROR, "SE: failed to scan all se tables from data dictionary!");
  }

  DBUG_RETURN(0);
}

/** Bind the dynamic table INFORMATION_SCHEMA.se_columns
@param[in,out]	p	table schema object
@return 0 on success */
static int se_columns_init(void *p) {
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("se_columns_init");

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    schema = (ST_SCHEMA_TABLE *)p;

    schema->fields_info = se_columns_fields_info;
    schema->fill_table = i_s_se_columns_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_SUBTABLE dynamic table
 */
namespace SMARTENGINE_SUBTABLE_FIELD {
enum {
  TABLE_NAME = 0,
  SUBTABLE_NAME,
  SUBTABLE_ID,
  TABLE_SPACE_ID,
  LEVEL,
  LAYER,
  EXTENT_COUNT,
  RAW_DATA_SIZE,
  COMPRESSED_DATA_SIZE,
  RAW_INDEX_SIZE,
  COMPRESSED_INDEX_SIZE,
  ACCESS_COUNT,
  READ,
  WRITE
};
} // namespace SMARTENGINE_SUBTABLE_FIELD

static ST_FIELD_INFO se_i_s_se_subtable_fields_info[] = {
    // format as schema_name.table_name
    SE_FIELD_INFO("TABLE_NAME", 2 * (NAME_LEN + 1), MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("SUBTABLE_NAME", NAME_LEN + 1, MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("SUBTABLE_ID", sizeof(int64_t), MYSQL_TYPE_LONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("TABLE_SPACE_ID", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LEVEL", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("LAYER", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("EXTENT_COUNT", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("RAW_DATA_SIZE", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("COMPRESSED_DATA_SIZE", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("RAW_INDEX_SIZE", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("COMPRESSED_INDEX_SIZE", sizeof(int32_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("ACCESS_COUNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    //SE_FIELD_INFO("CACHED_SIZE", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
    SE_FIELD_INFO("READ", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("WRITE", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO_END};

static int se_i_s_se_subtable_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  int ret = smartengine::common::Status::kOk;
  smartengine::db::DB *const se_db = get_se_db();
  std::vector<smartengine::db::ColumnFamilyHandle*> subtables;
  se_db->get_all_subtable(subtables);

  // collect subtables from dd
  std::map<uint32_t, std::pair<std::string, std::string>> subtable_id_map;
  if (SeDdHelper::get_se_subtable_map(thd, subtable_id_map)) {
    HANDLER_LOG(ERROR, "SE: failed to collection subtables from dd");
  }
  subtable_id_map.emplace(0, std::make_pair("<internal>", DEFAULT_CF_NAME));
  subtable_id_map.emplace(DEFAULT_SYSTEM_SUBTABLE_ID,
                          std::make_pair("<internal>", DEFAULT_SYSTEM_SUBTABLE_NAME));
  static std::string unknown_subtable("<Unknown>");

  // free the handle and release the cfd reference
  smartengine::db::SuperVersion *sv = nullptr;
  smartengine::storage::StorageManager *storage_manager = nullptr;
  uint64_t extent_count = 0;
  uint64_t raw_data_size = 0;
  uint64_t compressed_data_size = 0;
  uint64_t raw_index_size = 0;
  uint64_t compressed_index_size = 0;
  uint64_t layers = 0;
  int64_t pos = 0;
  smartengine::util::Arena arena;
  std::unique_ptr<smartengine::table::InternalIterator,
                  smartengine::memory::ptr_destruct<smartengine::table::InternalIterator>>
      iter;
  smartengine::common::ReadOptions read_options;
  smartengine::db::InternalStats *internal_stats = nullptr;
  smartengine::db::SubTable *st = nullptr;

  for (auto subtable : subtables) {
    if (nullptr == subtable) {
      continue;
    }
    st = static_cast<smartengine::db::ColumnFamilyHandleImpl*>(subtable)->cfd();
    sv = se_db->GetAndRefSuperVersion(st);
    if (nullptr == sv) {
      continue;
    }
    std::string table_name, index_name;
    uint32_t subtable_id = sv->cfd_.load()->GetID();
    auto name_it = subtable_id_map.find(subtable_id);
    if (name_it != subtable_id_map.end()) {
      table_name = name_it->second.first;
      index_name = name_it->second.second;
    } else {
      table_name = index_name = unknown_subtable;
    }
    storage_manager = sv->cfd_.load()->get_storage_manager();
    // FIXME don't iterate the meta to get the extent info
    for (int32_t level = 0; level < smartengine::storage::MAX_TIER_COUNT; level++) {
      iter.reset(storage_manager->get_single_level_iterator(
          read_options, &arena, sv->current_meta_, level));
      if (iter != nullptr) {
        extent_count = 0;
        raw_data_size = 0;
        compressed_data_size = 0;
        raw_index_size = 0;
        compressed_index_size = 0;
        iter->SeekToFirst();
        // iterate one level to get all the extents info
        while (smartengine::common::Status::kOk == ret && iter->Valid()) {
          smartengine::common::Slice extent_meta_buf = iter->key();
          ++extent_count;
          raw_data_size += ((storage::ExtentMeta*)extent_meta_buf.data())->raw_data_size_; 
          compressed_data_size += ((storage::ExtentMeta*)extent_meta_buf.data())->data_size_;
          raw_index_size += ((storage::ExtentMeta*)extent_meta_buf.data())->index_block_handle_.get_raw_size(); 
          compressed_index_size += ((storage::ExtentMeta*)extent_meta_buf.data())->index_block_handle_.get_size();
          iter->Next();
        }
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::TABLE_NAME]->store(
            table_name.c_str(), table_name.size(), system_charset_info);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::SUBTABLE_NAME]->store(
            index_name.c_str(), index_name.size(), system_charset_info);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::SUBTABLE_ID]->store(
            subtable_id, true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::TABLE_SPACE_ID]->store(
            sv->cfd_.load()->get_table_space_id(), true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::LEVEL]->store(level, true);
        if (0 == level) {
          layers = (sv->current_meta_->get_extent_layer_version(0) != nullptr)
                       ? sv->current_meta_->get_extent_layer_version(0)->get_extent_layer_size()
                       : 0;
        } else {
          layers = 1;
        }
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::LAYER]->store(layers, true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::EXTENT_COUNT]->store(extent_count, true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::RAW_DATA_SIZE]->store(raw_data_size, true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::COMPRESSED_DATA_SIZE]->store(compressed_data_size, true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::RAW_INDEX_SIZE]->store(raw_index_size, true);
        tables->table->field[SMARTENGINE_SUBTABLE_FIELD::COMPRESSED_INDEX_SIZE]->store(compressed_index_size, true);
        internal_stats = st->internal_stats();
        if (internal_stats != nullptr) {
          tables->table->field[SMARTENGINE_SUBTABLE_FIELD::ACCESS_COUNT]->store(
          internal_stats->get_cf_stats_count(smartengine::db::InternalStats::ACCESS_CNT), true);
          tables->table->field[SMARTENGINE_SUBTABLE_FIELD::READ]->store(
          internal_stats->get_cf_stats_value(smartengine::db::InternalStats::BYTES_READ), true);
          tables->table->field[SMARTENGINE_SUBTABLE_FIELD::WRITE]->store(
          internal_stats->get_cf_stats_value(smartengine::db::InternalStats::BYTES_WRITE), true);
        } else {
          tables->table->field[SMARTENGINE_SUBTABLE_FIELD::ACCESS_COUNT]->store(0, true);
          tables->table->field[SMARTENGINE_SUBTABLE_FIELD::READ]->store(0, true);
          tables->table->field[SMARTENGINE_SUBTABLE_FIELD::WRITE]->store(0, true);
        }

        ret = my_core::schema_table_store_record(thd, tables->table);
      }
    }
    se_db->ReturnAndCleanupSuperVersion(sv->cfd_, sv);
  }
  // release the reference and space
  se_db->return_all_subtable(subtables);

  DBUG_RETURN(ret);
}

/* Initialize the information_schema.se_subtable virtual table */
static int se_i_s_se_subtable_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_se_subtable_fields_info;
    schema->fill_table = se_i_s_se_subtable_fill_table;
  }

  DBUG_RETURN(ret);
}

/*
  Support for INFORMATION_SCHEMA.SMARTENGINE_QUERY_TRACE dynamic table
 */
namespace SMARTENGINE_QUERY_TRACE_FIELD {
enum {
  NAME = 0,
  TYPE,
  COST,
  COST_US,
  PERCENT,
  COUNT,
  COST_US_PER_QUERY,
  COUNT_PER_QUERY,
};
const int64_t MAX_NAME_SIZE = 256;
const int64_t MAX_TYPE_SIZE = 10;
} // namespace SMARTENGINE_QUERY_TRACE_FIELD

static ST_FIELD_INFO se_i_s_query_trace_fields_info[] = {
    SE_FIELD_INFO("NAME", SMARTENGINE_QUERY_TRACE_FIELD::MAX_NAME_SIZE,
                       MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("TYPE", SMARTENGINE_QUERY_TRACE_FIELD::MAX_TYPE_SIZE,
                       MYSQL_TYPE_STRING, 0),
    SE_FIELD_INFO("COST", sizeof(double), MYSQL_TYPE_DOUBLE, MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO("COST_US", sizeof(double), MYSQL_TYPE_DOUBLE, MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO("PERCENT", sizeof(double), MYSQL_TYPE_DOUBLE, MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO("COUNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG, MY_I_S_UNSIGNED),
    SE_FIELD_INFO("COST_US_PER_QUERY", sizeof(double), MYSQL_TYPE_DOUBLE, MY_I_S_MAYBE_NULL),
    SE_FIELD_INFO("COUNT_PER_QUERY", sizeof(double), MYSQL_TYPE_DOUBLE, 0),
    SE_FIELD_INFO_END};

static int se_i_s_query_trace_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();
  int ret = smartengine::common::Status::kOk;
  constexpr int32_t SQL_STATS_COUNT = 4;
  constexpr int32_t MAX_TRACE_POINT =
      static_cast<int32_t>(smartengine::monitor::TracePoint::QUERY_TIME_MAX_VALUE);
  constexpr int32_t MAX_ENGINE_COUNT =
      static_cast<int32_t>(smartengine::monitor::CountPoint::QUERY_COUNT_MAX_VALUE);
  constexpr int32_t MAX_COUNT_POINT = MAX_ENGINE_COUNT + SQL_STATS_COUNT;

  // get stats from QueryPerfContext
  uint64_t trace_time_i[MAX_TRACE_POINT];
  int64_t trace_count_i[MAX_TRACE_POINT];
  int64_t counter_i[MAX_COUNT_POINT];
  memset(trace_time_i, 0x00, sizeof(trace_time_i[0]) * MAX_TRACE_POINT);
  memset(trace_count_i, 0x00, sizeof(trace_count_i[0]) * MAX_TRACE_POINT);
  memset(counter_i, 0x00, sizeof(counter_i[0]) * MAX_COUNT_POINT);
  auto trace_ctx = smartengine::monitor::get_tls_query_perf_context();
  trace_ctx->get_global_trace_info(trace_time_i, trace_count_i);
  for (int32_t i = 0 ; i < MAX_ENGINE_COUNT; ++i) {
    auto count_point = static_cast<smartengine::monitor::CountPoint>(i);
    counter_i[i] = trace_ctx->get_global_count(count_point);
  }

  System_status_var status_totals;

  if (thd->fill_status_recursion_level++ == 0) {
    mysql_mutex_lock(&LOCK_status);
  }
  PFS_connection_status_visitor visitor(&status_totals);
  PFS_connection_iterator::visit_global(false, /* hosts */
                                        false, /* users */
                                        false, /* accounts */
                                        false, /* threads */
                                        true,  /* THDs */
                                        &visitor);

  int64_t sql_cmd_type[] = {SQLCOM_SELECT, SQLCOM_INSERT, SQLCOM_UPDATE,
                          SQLCOM_DELETE};
  const char *sql_cmd_name[] = {"SQLCOM_SELECT", "SQLCOM_INSERT", "SQLCOM_UPDATE",
                              "SQLCOM_DELETE"};

  for (int i = MAX_ENGINE_COUNT; i < MAX_COUNT_POINT; ++i) {
    int64_t cmd = sql_cmd_type[i - MAX_ENGINE_COUNT];
    counter_i[i] = status_totals.com_stat[cmd];
  }

  if (thd->fill_status_recursion_level-- == 1) {
    mysql_mutex_unlock(&LOCK_status);
  }

  // We need to calculate average and convert unit to second, so use double
  // instead of int.

  double trace_time[MAX_TRACE_POINT];
  double trace_count[MAX_TRACE_POINT];
  double counter[MAX_COUNT_POINT];
  double trace_time_sum = 0.0;

  for (int32_t i = 0; i < MAX_TRACE_POINT; ++i) {
    trace_time[i] = static_cast<double>(trace_time_i[i]);
    trace_time_sum += trace_time[i];
  }

  for (int32_t i = 0; i < MAX_TRACE_POINT; ++i) {
    trace_count[i] = static_cast<double>(trace_count_i[i]);
  }

  // The first trace point is SERVER_OPERATION which hits once per query.
  double query_count = trace_count[0];

  for (int32_t i = 0; i < MAX_COUNT_POINT; ++i) {
    counter[i] = static_cast<double>(counter_i[i]);
  }


  static double cycles_per_second = static_cast<double>(
        smartengine::monitor::get_trace_unit(1000 /* 1000 milli second */));
  static double cycles_per_us = cycles_per_second * 1e-6;

  if (0 == cycles_per_second) {
    sql_print_error("query trace get_trace_unit returns 0");
  } else if (0 == query_count) {
    sql_print_warning("query count is 0, can not fill SMARTENGINE_QUERY_TRACE table");
  } else if (0 == trace_time_sum) {
    sql_print_warning("trace time sum is 0, can not fill SMARTENGINE_QUERY_TRACE table");
  } else {
    // output stats to IS table.
    Field **field_array = tables->table->field;
    const char **trace_point_name = smartengine::monitor::get_trace_point_name();
    const char **count_point_name = smartengine::monitor::get_count_point_name();
    const char *type_name = "TRACE";
    int64_t type_name_len = strlen(type_name);
    for (int32_t i = 0; i < MAX_TRACE_POINT; ++i) {
      double trace_time_in_sec = trace_time[i] / cycles_per_second;
      double trace_time_in_us = trace_time[i] / cycles_per_us;

      field_array[SMARTENGINE_QUERY_TRACE_FIELD::NAME]->store(trace_point_name[i],
                                                  strlen(trace_point_name[i]),
                                                  system_charset_info);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::TYPE]->store(type_name,
                                                      type_name_len,
                                                      system_charset_info);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST]->set_notnull();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST]->store(trace_time_in_sec);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST_US]->set_notnull();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST_US]->store(trace_time_in_us);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::PERCENT]->set_notnull();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::PERCENT]->store(
                                        trace_time[i] / trace_time_sum * 100.0);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COUNT]->store(trace_count_i[i], true);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST_US_PER_QUERY]->set_notnull();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST_US_PER_QUERY]->store(
                                        trace_time_in_us / query_count);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COUNT_PER_QUERY]->store(
                                        trace_count[i] / query_count);
      my_core::schema_table_store_record(thd, tables->table);
    }

    type_name = "COUNTER";
    type_name_len = strlen(type_name);
    for (int32_t i = 0; i < MAX_COUNT_POINT; ++i) {
      const char *counter_name = nullptr;
      if (i < MAX_ENGINE_COUNT) {
        counter_name = count_point_name[i];
      } else {
        counter_name = sql_cmd_name[i - MAX_ENGINE_COUNT];
      }
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::NAME]->store(counter_name,
                                                  strlen(counter_name),
                                                  system_charset_info);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::TYPE]->store(type_name,
                                                      type_name_len,
                                                      system_charset_info);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST]->set_null();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST_US]->set_null();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::PERCENT]->set_null();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COUNT]->store(counter_i[i], true);
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COST_US_PER_QUERY]->set_null();
      field_array[SMARTENGINE_QUERY_TRACE_FIELD::COUNT_PER_QUERY]->store(
                                            counter[i] / query_count);
      my_core::schema_table_store_record(thd, tables->table);
    }
  }

  DBUG_RETURN(ret);
}

/* Initialize the information_schema.se_subtable virtual table */
static int se_i_s_query_trace_init(void *const p) {
  DBUG_ENTER_FUNC();

  assert(p != nullptr);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_query_trace_fields_info;
    schema->fill_table = se_i_s_query_trace_fill_table;
  }

  DBUG_RETURN(ret);
}

namespace SMARTENGINE_TABLE_SPACE_FIELD {
enum {
  TABLE_SPACE_ID = 0,
  EXTENT_SPACE_TYPE,
  FILE_NUMBER,
  TOTAL_EXTENT_COUNT,
  USED_EXTENT_COUNT,
  FREE_EXTENT_COUNT
};
} // namespace SMARTENGINE_TABLE_SPACE_FIELD

static int se_i_s_se_table_space_fill_one_row(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    const smartengine::storage::DataFileStatistics &data_file_statistics) {
  tables->table->field[SMARTENGINE_TABLE_SPACE_FIELD::TABLE_SPACE_ID]->store(
      data_file_statistics.table_space_id_, false);
  tables->table->field[SMARTENGINE_TABLE_SPACE_FIELD::EXTENT_SPACE_TYPE]->store(
      data_file_statistics.extent_space_type_, false);
  tables->table->field[SMARTENGINE_TABLE_SPACE_FIELD::FILE_NUMBER]->store(
      data_file_statistics.file_number_, false);
  tables->table->field[SMARTENGINE_TABLE_SPACE_FIELD::TOTAL_EXTENT_COUNT]
      ->store(data_file_statistics.total_extent_count_, false);
  tables->table->field[SMARTENGINE_TABLE_SPACE_FIELD::USED_EXTENT_COUNT]->store(
      data_file_statistics.used_extent_count_, false);
  tables->table->field[SMARTENGINE_TABLE_SPACE_FIELD::FREE_EXTENT_COUNT]->store(
      data_file_statistics.free_extent_count_, false);

  return my_core::schema_table_store_record(thd, tables->table);
}
static int se_i_s_se_table_space_fill_table(
    my_core::THD *const thd, my_core::Table_ref *const tables,
    my_core::Item *const cond MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();
  int ret = smartengine::common::Status::kOk;
  std::vector<smartengine::storage::DataFileStatistics> data_file_stats;
  smartengine::db::DB *const se_db = get_se_db();

  if (smartengine::common::Status::kOk == (ret = se_db->get_data_file_stats(data_file_stats))) {
    for (uint32_t i = 0; smartengine::common::Status::kOk == ret && i < data_file_stats.size(); ++i) {
      ret = se_i_s_se_table_space_fill_one_row(thd, tables, data_file_stats.at(i));
    }
  }
  DBUG_RETURN(ret);
}

static ST_FIELD_INFO se_i_s_se_table_space_fields_info[] = {
  SE_FIELD_INFO("TABLE_SPACE_ID", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("EXTENT_SPACE_TYPE", sizeof(int32_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("FILE_NUMBER", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("TOTAL_EXTENT_COUNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("USED_EXTENT_COUNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("FREE_EXTENT_COUNT", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO_END};
/* Initialize the information_schema.se_table_space virtual table */
static int se_i_s_se_table_space_init(void *const p)
{
  DBUG_ENTER_FUNC();

  assert(nullptr != p);

  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_se_table_space_fields_info;
    schema->fill_table = se_i_s_se_table_space_fill_table;
  }

  DBUG_RETURN(ret);
}


static struct st_mysql_information_schema se_i_s_info = {
    MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION};

struct st_mysql_plugin se_i_s_cfstats = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "CFSTATS",
    "Alibaba",
    "SE sub table stats",
    PLUGIN_LICENSE_GPL,
    se_i_s_cfstats_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_dbstats = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "DBSTATS",
    "Alibaba",
    "SE database stats",
    PLUGIN_LICENSE_GPL,
    se_i_s_dbstats_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_perf_context = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "PERF_CONTEXT",
    "Alibaba",
    "SE perf context stats",
    PLUGIN_LICENSE_GPL,
    se_i_s_perf_context_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_perf_context_global = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "PERF_CONTEXT_GLOBAL",
    "Alibaba",
    "SE perf context stats in global",
    PLUGIN_LICENSE_GPL,
    se_i_s_perf_context_global_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_global_info = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "GLOBAL_INFO",
    "Alibaba",
    "SE global information",
    PLUGIN_LICENSE_GPL,
    se_i_s_global_info_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_compact_stats = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "COMPACTION_STATS",
    "Alibaba",
    "SE compaction stats",
    PLUGIN_LICENSE_GPL,
    se_i_s_compact_stats_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_ddl = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "DDL",
    "Alibaba",
    "SE Data Dictionary",
    PLUGIN_LICENSE_GPL,
    se_i_s_ddl_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_lock_info = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "LOCKS",
    "Alibaba",
    "SE lock information",
    PLUGIN_LICENSE_GPL,
    se_i_s_lock_info_init,
    nullptr,
    nullptr,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_trx_info = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "TRX",
    "Alibaba",
    "SE transaction information",
    PLUGIN_LICENSE_GPL,
    se_i_s_trx_info_init,
    nullptr,
    nullptr,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_se_compaction_task = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "COMPACTION_TASK",
    "Alibaba",
    "SE all running compaction tasks",
    PLUGIN_LICENSE_GPL,
    se_i_s_se_compaction_task_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_se_compaction_history = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "COMPACTION_HISTORY",
    "Alibaba",
    "SE all historical compaction tasks",
    PLUGIN_LICENSE_GPL,
    se_i_s_se_compaction_history_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_se_mem_alloc = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "MEM_ALLOC",
    "Alibaba",
    "SE memory allocate stats",
    PLUGIN_LICENSE_GPL,
    se_i_s_se_mem_alloc_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin i_s_se_tables = {
    /* the plugin type (a MYSQL_XXX_PLUGIN value) */
    /* int */
     MYSQL_INFORMATION_SCHEMA_PLUGIN,

    /* pointer to type-specific plugin descriptor */
    /* void* */
    &se_i_s_info,

    /* plugin name */
    /* const char* */
    SE_IS_PREFIX "TABLES",

    /* plugin author (for SHOW PLUGINS) */
    /* const char* */
    "Alibaba",

    /* general descriptive text (for SHOW PLUGINS) */
    /* const char* */
    "SE tables",

    /* the plugin license (PLUGIN_LICENSE_XXX) */
    /* int */
    PLUGIN_LICENSE_GPL,

    /* the function to invoke when plugin is loaded */
    /* int (*)(void*); */
    se_tables_init,

    /* the function to invoke when plugin is un installed */
    /* int (*)(void*); */
    nullptr,

    /* the function to invoke when plugin is unloaded */
    /* int (*)(void*); */
    se_i_s_deinit,

    /* plugin version (for SHOW PLUGINS) */
    /* unsigned int */
    0x0001,

    /* SHOW_VAR* */
    nullptr,

    /* SYS_VAR** */
    nullptr,

    /* reserved for dependency checking */
    /* void* */
    nullptr,

    /* Plugin flags */
    /* unsigned long */
    0,
};

struct st_mysql_plugin i_s_se_columns = {
    /* the plugin type (a MYSQL_XXX_PLUGIN value) */
    /* int */
    MYSQL_INFORMATION_SCHEMA_PLUGIN,

    /* pointer to type-specific plugin descriptor */
    /* void* */
    &se_i_s_info,

    /* plugin name */
    /* const char* */
    SE_IS_PREFIX "COLUMNS",

    /* plugin author (for SHOW PLUGINS) */
    /* const char* */
    "Alibaba",

    /* general descriptive text (for SHOW PLUGINS) */
    /* const char* */
    "SE table columns",

    /* the plugin license (PLUGIN_LICENSE_XXX) */
    /* int */
    PLUGIN_LICENSE_GPL,

    /* the function to invoke when plugin is loaded */
    /* int (*)(void*); */
    se_columns_init,

    /* the function to invoke when plugin is un installed */
    /* int (*)(void*); */
    nullptr,

    /* the function to invoke when plugin is unloaded */
    /* int (*)(void*); */
    se_i_s_deinit,

    /* plugin version (for SHOW PLUGINS) */
    /* unsigned int */
    0x0001,

    /* SHOW_VAR* */
    nullptr,

    /* SYS_VAR** */
    nullptr,

    /* reserved for dependency checking */
    /* void* */
    nullptr,

    /* Plugin flags */
    /* unsigned long */
    0,
};

struct st_mysql_plugin se_i_s_se_subtable = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "SUBTABLE",
    "Alibaba",
    "SMARTENGINE subtable information",
    PLUGIN_LICENSE_GPL,
    se_i_s_se_subtable_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

struct st_mysql_plugin se_i_s_query_trace = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "QUERY_TRACE",
    "Alibaba",
    "SMARTENGINE internal performance trace info",
    PLUGIN_LICENSE_GPL,
    se_i_s_query_trace_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

namespace SMARTENGINE_DEBUG_INFO {
enum {
  DEBUG_KEY = 0,
  KEY_DESC,
  ITEM_ID,
  TIMESTAMP,
  INFO_1,
  INFO_2,
  INFO_3,
  INFO_4,
  INFO_5,
  INFO_6,
};
const int64_t VALUE_VARCHAR_LEN = 1024;
const int64_t DESC_VARCHAR_LEN = 256;
}

static ST_FIELD_INFO se_i_s_debug_info_fields_info[] = {
  SE_FIELD_INFO("DEBUG_KEY", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("KEY_DESC", SMARTENGINE_DEBUG_INFO::DESC_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("ITEM_ID", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("TIMESTAMP", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
  SE_FIELD_INFO("INFO_1", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("INFO_2", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("INFO_3", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("INFO_4", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("INFO_5", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO("INFO_6", SMARTENGINE_DEBUG_INFO::VALUE_VARCHAR_LEN, MYSQL_TYPE_STRING, 0),
  SE_FIELD_INFO_END
};

class SeDebugInfoFiller {
public:
  SeDebugInfoFiller(THD *t, Table_ref *tl) : thd_(t), tables_(tl) {}
  ~SeDebugInfoFiller() {}
  int operator()(const std::string &key, const smartengine::common::DebugInfoEntry &entry) {
    Field **field = tables_->table->field;
    field[SMARTENGINE_DEBUG_INFO::DEBUG_KEY]->store(key.c_str(), key.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::KEY_DESC]->store(entry.key_desc_.c_str(), entry.key_desc_.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::ITEM_ID]->store(entry.item_id_, true);
    field[SMARTENGINE_DEBUG_INFO::TIMESTAMP]->store(entry.timestamp_, true);
    field[SMARTENGINE_DEBUG_INFO::INFO_1]->store(entry.debug_info_1_.c_str(), entry.debug_info_1_.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::INFO_2]->store(entry.debug_info_2_.c_str(), entry.debug_info_2_.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::INFO_3]->store(entry.debug_info_3_.c_str(), entry.debug_info_3_.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::INFO_4]->store(entry.debug_info_4_.c_str(), entry.debug_info_4_.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::INFO_5]->store(entry.debug_info_5_.c_str(), entry.debug_info_5_.size(), system_charset_info);
    field[SMARTENGINE_DEBUG_INFO::INFO_6]->store(entry.debug_info_6_.c_str(), entry.debug_info_6_.size(), system_charset_info);
    return my_core::schema_table_store_record(thd_, tables_->table);
  }
private:
  THD *thd_;
  Table_ref* tables_;
};

static int se_i_s_debug_info_fill_table(THD *thd, Table_ref *tables, Item *) {
  DBUG_ENTER_FUNC();

  SeDebugInfoFiller filler(thd, tables);
  smartengine::common::DebugInfoStation::get_instance()->foreach(filler);

  DBUG_RETURN(0);
}

static int se_i_s_se_debug_info_init(void *const p) {
  DBUG_ENTER_FUNC();
  assert(p != nullptr);
  int ret = 0;
  if (!se_is_initialized()) {
    ret = 1;
  } else {
    my_core::ST_SCHEMA_TABLE *schema;
    schema = (my_core::ST_SCHEMA_TABLE *)p;
    schema->fields_info = se_i_s_debug_info_fields_info;
    schema->fill_table = se_i_s_debug_info_fill_table;
  }
  DBUG_RETURN(ret);
}

struct st_mysql_plugin se_i_s_se_debug_info = {
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &se_i_s_info,
  SE_IS_PREFIX "DEBUG_INFO",
  "Alibaba",
  "schemaless se debug info",
  PLUGIN_LICENSE_GPL,
  se_i_s_se_debug_info_init,
  nullptr,   /* check_uninstall */
  se_i_s_deinit,
  0x0001,   /* version number  */
  nullptr,  /* status variables */
  nullptr,  /* system variables */
  nullptr,  /* config options */
  0,        /* flags */
};

struct st_mysql_plugin se_i_s_se_table_space = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &se_i_s_info,
    SE_IS_PREFIX "TABLE_SPACE",
    "Alibaba",
    "SE table space information",
    PLUGIN_LICENSE_GPL,
    se_i_s_se_table_space_init,
    nullptr,
    se_i_s_deinit,
    0x0001,  /* version number (0.1) */
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
};

} //namespace smartengine
