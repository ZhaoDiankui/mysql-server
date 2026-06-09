/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited

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

#include <sys/types.h>

#include "consensus_log_manager.h"
#include "consensus_state_process.h"
#include "i_s_consensus.h"
#include "plugin.h"
#include "rpl_consensus.h"
#include "system_variables.h"

#include "my_dbug.h"
#include "mysql/plugin.h"
#include "sql/auth/auth_acls.h"
#include "sql/field.h"
#include "sql/sql_class.h"
#include "sql/sql_show.h"
#include "sql/table.h"

const char plugin_author[] = PLUGIN_AUTHOR;

const uint8_t i_s_plugin_version_postfix = 1;

const uint64_t i_s_plugin_version =
    PLUGIN_VERSION << 8 | i_s_plugin_version_postfix;

static struct st_mysql_information_schema i_s_info = {
    MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION};

/** I_S.innodb_* views version postfix. Everytime the define of any I_S
table is changed, this value has to be increased accordingly */
constexpr uint8_t i_s_consensus_replication_plugin_version_postfix = 2;

#define OK(expr)     \
  if ((expr) != 0) { \
    return 1;        \
  }

#if !defined __STRICT_ANSI__ && defined __GNUC__ && !defined __clang__
#define STRUCT_FLD(name, value) \
  name:                         \
  value
#else
#define STRUCT_FLD(name, value) value
#endif

/** Unbind a dynamic INFORMATION_SCHEMA table. */
static int i_s_common_deinit(void *) {
  DBUG_TRACE;
  /* Do nothing */
  return 0;
}

int fill_wesql_cluster_global(THD *thd, Table_ref *tables, Item *) {
  int res = 0;
  TABLE *table = tables->table;
  std::string has_voted;
  std::string force_sync;
  std::string pipelining;
  std::string use_applied;
  std::vector<cluster_global_info> cgis;
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return res;

  // if node is not LEADER, global information is empty
  if (rpl_consensus_get_cluster_global_info(&cgis, true) == 0) {
    if (schema_table_store_record(thd, table))
      return 1;
    else
      return res;
  }

  for (auto e : cgis) {
    int field_num = 0;
    switch (e.has_voted) {
      case 1:
        has_voted = "Yes";
        break;
      case 0:
        has_voted = "No";
        break;
    }
    switch (e.force_sync) {
      case 1:
        force_sync = "Yes";
        break;
      case 0:
        force_sync = "No";
        break;
    }
    if (e.pipelining) {
      pipelining = "Yes";
    } else {
      pipelining = "No";
    }
    if (e.use_applied) {
      use_applied = "Yes";
    } else {
      use_applied = "No";
    }
    table->field[field_num++]->store((longlong)e.server_id, true);
    table->field[field_num++]->store(e.ip_port.c_str(), e.ip_port.length(),
                                     system_charset_info);
    table->field[field_num++]->store((longlong)e.match_index, true);
    table->field[field_num++]->store((longlong)e.next_index, true);
    table->field[field_num++]->store(e.role.c_str(), e.role.length(),
                                     system_charset_info);
    table->field[field_num++]->store(has_voted.c_str(), has_voted.length(),
                                     system_charset_info);
    table->field[field_num++]->store(force_sync.c_str(), force_sync.length(),
                                     system_charset_info);
    table->field[field_num++]->store((longlong)e.election_weight, true);
    table->field[field_num++]->store((longlong)e.learner_source, true);
    table->field[field_num++]->store((longlong)e.applied_index, true);
    table->field[field_num++]->store(pipelining.c_str(), pipelining.length(),
                                     system_charset_info);
    table->field[field_num++]->store(use_applied.c_str(), use_applied.length(),
                                     system_charset_info);
    if (schema_table_store_record(thd, table)) return 1;
  }

  return res;
}

int fill_wesql_cluster_local(THD *thd, Table_ref *tables, Item *) {
  int res = 0;
  TABLE *table = tables->table;
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return res;

  // get from local log manager
  Consensus_Log_System_Status rw_status = consensus_state_process.get_status();
  std::string rw_status_str;
  std::string instance_type;
  cluster_local_info cli;
  rpl_consensus_get_cluster_local_info(&cli);
  /* not a stable leader if server_ready_for_rw is no and
   * consensus_auto_leader_transfer is on */
  if (cli.role == "Leader" && opt_consensus_auto_leader_transfer &&
      (opt_cluster_log_type_instance ||
       rw_status == Consensus_Log_System_Status::RELAY_LOG_WORKING))
    cli.role = "Prepared";

  if (opt_cluster_log_type_instance) {
    rw_status_str = "No";
  } else {
    switch (rw_status) {
      case Consensus_Log_System_Status::BINLOG_WORKING:
        rw_status_str = "Yes";
        break;
      case Consensus_Log_System_Status::RELAY_LOG_WORKING:
        rw_status_str = "No";
        break;
    }
  }

  switch (opt_cluster_log_type_instance) {
    case 0:
      instance_type = "Normal";
      break;
    case 1:
      instance_type = "Log";
      break;
  }

  int field_num = 0;
  table->field[field_num++]->store((longlong)cli.server_id, true);
  table->field[field_num++]->store((longlong)cli.current_term, true);
  table->field[field_num++]->store(cli.current_leader_ip_port.c_str(),
                                   cli.current_leader_ip_port.length(),
                                   system_charset_info);
  table->field[field_num++]->store((longlong)cli.commit_index, true);
  table->field[field_num++]->store((longlong)cli.last_log_term, true);
  table->field[field_num++]->store((longlong)cli.last_log_index, true);
  table->field[field_num++]->store(cli.role.c_str(), cli.role.length(),
                                   system_charset_info);
  table->field[field_num++]->store((longlong)cli.voted_for, true);
  table->field[field_num++]->store((longlong)cli.last_apply_index, true);
  table->field[field_num++]->store(rw_status_str.c_str(),
                                   rw_status_str.length(), system_charset_info);
  table->field[field_num++]->store(instance_type.c_str(),
                                   instance_type.length(), system_charset_info);

  if (schema_table_store_record(thd, table)) return 1;

  return res;
}

int fill_wesql_cluster_health(THD *thd, Table_ref *tables, Item *) {
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return 0;
  TABLE *table = tables->table;

  std::vector<cluster_helthy_info> hi;
  if (rpl_consensus_get_helthy_info(&hi) == 0) return 0;

  for (auto e : hi) {
    int field_num = 0;
    std::string connected;
    table->field[field_num++]->store((longlong)e.server_id, true);
    table->field[field_num++]->store(e.addr.c_str(), e.addr.length(),
                                     system_charset_info);
    table->field[field_num++]->store(e.role.c_str(), e.role.length(),
                                     system_charset_info);
    if (e.connected) {
      connected = "YES";
    } else {
      connected = "NO";
    }
    table->field[field_num++]->store(connected.c_str(), connected.length(),
                                     system_charset_info);
    table->field[field_num++]->store((longlong)e.log_delay, true);
    table->field[field_num++]->store((longlong)e.apply_delay, true);

    if (schema_table_store_record(thd, table)) return 1;
  }

  return 0;
}

int fill_wesql_cluster_learner_source(THD *thd, Table_ref *tables, Item *) {
  int res = 0;
  TABLE *table = tables->table;
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return res;

  // get from consensus alg layer
  uint64 learner_id = 0;
  uint64 source_id = 0;
  std::string learner_ip_port;
  std::string source_ip_port;
  uint64 source_last_index;
  uint64 source_commit_index;
  uint64 learner_match_index = 0;
  uint64 learner_next_index = 0;

  uint64 learner_applied_index = 0;
  uint64 learner_source = 0;

  size_t node_num = 1;

  uint learner_source_count = 0;

  cluster_local_info mi;
  std::vector<cluster_global_info> cis;

  rpl_consensus_get_cluster_local_info(&mi);
  rpl_consensus_get_cluster_global_info(&cis, false);
  node_num = cis.size();

  source_id = mi.server_id;

  for (uint i = 0; i < node_num; i++) {
    learner_source = cis[i].learner_source;
    if (cis[i].server_id == source_id) source_ip_port = cis[i].ip_port;
    if (learner_source != source_id)
      continue;
    else
      learner_source_count++;

    learner_id = cis[i].server_id;
    learner_ip_port = cis[i].ip_port;
    source_last_index = mi.last_log_index;
    source_commit_index = mi.commit_index;
    learner_match_index = cis[i].match_index;
    learner_next_index = cis[i].next_index;
    learner_applied_index = cis[i].applied_index;

    int field_num = 0;
    table->field[field_num++]->store((longlong)learner_id, true);
    table->field[field_num++]->store(
        learner_ip_port.c_str(), learner_ip_port.length(), system_charset_info);
    table->field[field_num++]->store((longlong)source_id, true);
    table->field[field_num++]->store(
        source_ip_port.c_str(), source_ip_port.length(), system_charset_info);
    table->field[field_num++]->store((longlong)source_last_index, true);
    table->field[field_num++]->store((longlong)source_commit_index, true);
    table->field[field_num++]->store((longlong)learner_match_index, true);
    table->field[field_num++]->store((longlong)learner_next_index, true);
    table->field[field_num++]->store((longlong)learner_applied_index, true);
    if (schema_table_store_record(thd, table)) return 1;
  }
  if (learner_source_count == 0) {
    if (schema_table_store_record(thd, table)) return 1;
  }

  return res;
}

int fill_wesql_cluster_prefetch_channel(THD *thd, Table_ref *tables, Item *) {
  int res = 0;
  TABLE *table = tables->table;
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return res;

  // get from consensus alg layer
  ConsensusPreFetchManager *prefetch_mgr =
      consensus_log_manager.get_prefetch_manager();
  prefetch_mgr->lock_prefetch_channels_hash(true);
  for (auto iter = prefetch_mgr->get_channels_hash()->begin();
       iter != prefetch_mgr->get_channels_hash()->end(); ++iter) {
    int field_num = 0;
    table->field[field_num++]->store((longlong)iter->second->get_channel_id(),
                                     true);
    table->field[field_num++]->store(
        (longlong)iter->second->get_first_index_in_cache(), true);
    table->field[field_num++]->store(
        (longlong)iter->second->get_last_index_in_cache(), true);
    table->field[field_num++]->store(
        (longlong)iter->second->get_prefetch_cache_size(), true);
    table->field[field_num++]->store(
        (longlong)iter->second->get_current_request(), true);

    if (schema_table_store_record(thd, table)) return 1;
  }

  prefetch_mgr->unlock_prefetch_channels_hash();
  return res;
}

int fill_wesql_cluster_consensus_status(THD *thd, Table_ref *tables, Item *) {
  int res = 0;
  TABLE *table = tables->table;
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return 0;

  // get from consensus alg layer
  uint64 id = server_id;
  cluster_stats cs;
  rpl_consensus_get_stats(&cs);

  int field_num = 0;
  table->field[field_num++]->store((longlong)id, true);
  table->field[field_num++]->store((longlong)cs.count_msg_append_log, true);
  table->field[field_num++]->store((longlong)cs.count_msg_request_vote, true);
  table->field[field_num++]->store((longlong)cs.count_heartbeat, true);
  table->field[field_num++]->store((longlong)cs.count_on_msg_append_log, true);
  table->field[field_num++]->store((longlong)cs.count_on_msg_request_vote,
                                   true);
  table->field[field_num++]->store((longlong)cs.count_on_heartbeat, true);
  table->field[field_num++]->store((longlong)cs.count_replicate_log, true);
  table->field[field_num++]->store((longlong)cs.count_log_meta_get_in_cache,
                                   true);
  table->field[field_num++]->store((longlong)cs.count_log_meta_get_total, true);
  if (schema_table_store_record(thd, table)) return 1;

  return res;
}

int fill_wesql_cluster_consensus_membership_change(THD *thd, Table_ref *tables,
                                                   Item *) {
  TABLE *table = tables->table;
  DBUG_TRACE;

  if (!is_consensus_replication_enabled()) return 0;

  std::vector<cluster_memebership_change> cmcs;
  rpl_consensus_get_member_changes(&cmcs);

  for (auto &e : cmcs) {
    int field_num = 0;
    table->field[field_num++]->store(e.time.c_str(), e.time.length(),
                                     system_charset_info);
    table->field[field_num++]->store(e.command.c_str(), e.command.length(),
                                     system_charset_info);
    if (schema_table_store_record(thd, table)) return 1;
  }

  return 0;
}

ST_FIELD_INFO wesql_cluster_global_fields_info[] = {
    {"SERVER_ID", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"IP_PORT", HOST_AND_PORT_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"MATCH_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"NEXT_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"ROLE", 10, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"HAS_VOTED", 3, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"FORCE_SYNC", 3, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"ELECTION_WEIGHT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0,
     0, 0},
    {"LEARNER_SOURCE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0,
     0, 0},
    {"APPLIED_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"PIPELINING", 3, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"SEND_APPLIED", 3, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

ST_FIELD_INFO wesql_cluster_local_fields_info[] = {
    {"SERVER_ID", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"CURRENT_TERM", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"CURRENT_LEADER", HOST_AND_PORT_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"COMMIT_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"LAST_LOG_TERM", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"LAST_LOG_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0,
     0, 0},
    {"ROLE", 10, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"VOTED_FOR", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"LAST_APPLY_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0,
     0, 0},
    {"SERVER_READY_FOR_RW", 3, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"INSTANCE_TYPE", 10, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

ST_FIELD_INFO wesql_cluster_health_fields_info[] = {
    {"SERVER_ID", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"IP_PORT", HOST_AND_PORT_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"ROLE", 10, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"CONNECTED", 3, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"LOG_DELAY_NUM", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"APPLY_DELAY_NUM", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0,
     0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

ST_FIELD_INFO wesql_cluster_learner_source_fields_info[] = {
    {"LEARNER_SERVER_ID", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"LEARNER_IP_PORT", HOST_AND_PORT_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"SOURCE_SERVER_ID", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"SOURCE_IP_PORT", HOST_AND_PORT_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"SOURCE_LAST_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0,
     0, 0, 0},
    {"SOURCE_COMMIT_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0,
     0, 0, 0},
    {"LEARNER_MATCH_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0,
     0, 0, 0},
    {"LEARNER_NEXT_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0,
     0, 0, 0},
    {"LEARNER_APPLIED_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
     0, 0, 0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

ST_FIELD_INFO wesql_cluster_prefetch_channel_info[] = {
    {"CHANNEL_ID", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"FIRST_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"LAST_INDEX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"CACHE_SIZE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {"CURRENT_REQUEST", NAME_CHAR_LEN, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

ST_FIELD_INFO wesql_cluster_consensus_status_fields_info[] = {
    {"SERVER_ID", 10, MYSQL_TYPE_LONG, 0, 0, 0, 0},
    {"COUNT_MSG_APPEND_LOG", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
     0, 0, 0, 0},
    {"COUNT_MSG_REQUEST_VOTE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
     0, 0, 0, 0},
    {"COUNT_HEARTBEAT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0,
     0, 0},
    {"COUNT_ON_MSG_APPEND_LOG", MY_INT64_NUM_DECIMAL_DIGITS,
     MYSQL_TYPE_LONGLONG, 0, 0, 0, 0},
    {"COUNT_ON_MSG_REQUEST_VOTE", MY_INT64_NUM_DECIMAL_DIGITS,
     MYSQL_TYPE_LONGLONG, 0, 0, 0, 0},
    {"COUNT_ON_HEARTBEAT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0,
     0, 0, 0},
    {"COUNT_REPLICATE_LOG", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0,
     0, 0, 0},
    {"COUNT_LOG_META_GET_IN_CACHE", MY_INT64_NUM_DECIMAL_DIGITS,
     MYSQL_TYPE_LONGLONG, 0, 0, 0, 0},
    {"COUNT_LOG_META_GET_TOTAL", MY_INT64_NUM_DECIMAL_DIGITS,
     MYSQL_TYPE_LONGLONG, 0, 0, 0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

ST_FIELD_INFO wesql_cluster_consensus_membership_change_fields_info[] = {
    {"TIME", 32, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"COMMAND", 512, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

/**
 * Bind the dynamic table INFORMATION_SCHEMA.***
 **/
static int wesql_cluster_global_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_global_fields_info;
  schema->fill_table = fill_wesql_cluster_global;
  return 0;
}

static int wesql_cluster_local_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_local_fields_info;
  schema->fill_table = fill_wesql_cluster_local;
  return 0;
}

static int wesql_cluster_health_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_health_fields_info;
  schema->fill_table = fill_wesql_cluster_health;
  return 0;
}

static int wesql_cluster_learner_source_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_learner_source_fields_info;
  schema->fill_table = fill_wesql_cluster_learner_source;
  return 0;
}

static int wesql_cluster_prefetch_channel_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_prefetch_channel_info;
  schema->fill_table = fill_wesql_cluster_prefetch_channel;
  return 0;
}

static int wesql_cluster_consensus_status_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_consensus_status_fields_info;
  schema->fill_table = fill_wesql_cluster_consensus_status;
  return 0;
}

static int wesql_cluster_consensus_membership_change_init(void *p) {
  ST_SCHEMA_TABLE *schema;
  DBUG_TRACE;
  schema = (ST_SCHEMA_TABLE *)p;
  schema->fields_info = wesql_cluster_consensus_membership_change_fields_info;
  schema->fill_table = fill_wesql_cluster_consensus_membership_change;
  return 0;
}

struct st_mysql_plugin i_s_wesql_cluster_global = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_GLOBAL",
    plugin_author,
    "Consensus Replication wesql_cluster_global",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_global_init, /* Plugin Init */
    nullptr,                   /* Plugin Check uninstall */
    i_s_common_deinit,         /* Plugin Deinit */
    i_s_plugin_version,        /* Plugin Version */
    nullptr,                   /* status variables */
    nullptr,                   /* system variables */
    nullptr,                   /* config options */
    0,                         /* flags */
};

struct st_mysql_plugin i_s_wesql_cluster_local = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_LOCAL",
    plugin_author,
    "Consensus peplication wesql_cluster_local",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_local_init, /* Plugin Init */
    nullptr,                  /* Plugin Check uninstall */
    i_s_common_deinit,        /* Plugin Deinit */
    i_s_plugin_version,       /* Plugin Version */
    nullptr,                  /* status variables */
    nullptr,                  /* system variables */
    nullptr,                  /* config options */
    0,                        /* flags */
};

struct st_mysql_plugin i_s_wesql_cluster_health = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_HEALTH",
    plugin_author,
    "Consensus Replication wesql_cluster_health",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_health_init, /* Plugin Init */
    nullptr,                   /* Plugin Check uninstall */
    i_s_common_deinit,         /* Plugin Deinit */
    i_s_plugin_version,        /* Plugin Version */
    nullptr,                   /* status variables */
    nullptr,                   /* system variables */
    nullptr,                   /* config options */
    0,                         /* flags */
};

struct st_mysql_plugin i_s_wesql_cluster_learner_source = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_LEARNER_SOURCE",
    plugin_author,
    "Consensus Replication wesql_cluster_learner_source",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_learner_source_init, /* Plugin Init */
    nullptr,                   /* Plugin Check uninstall */
    i_s_common_deinit,         /* Plugin Deinit */
    i_s_plugin_version,        /* Plugin Version */
    nullptr,                   /* status variables */
    nullptr,                   /* system variables */
    nullptr,                   /* config options */
    0,                         /* flags */
};

struct st_mysql_plugin i_s_wesql_cluster_prefetch_channel = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_PREFETCH_CHANNEL",
    plugin_author,
    "Consensus Replication wesql_cluster_prefetch_channel",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_prefetch_channel_init, /* Plugin Init */
    nullptr,                   /* Plugin Check uninstall */
    i_s_common_deinit,         /* Plugin Deinit */
    i_s_plugin_version,        /* Plugin Version */
    nullptr,                   /* status variables */
    nullptr,                   /* system variables */
    nullptr,                   /* config options */
    0,                         /* flags */
};

struct st_mysql_plugin i_s_wesql_cluster_consensus_status = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_CONSENSUS_STATUS",
    plugin_author,
    "Consensus Replication wesql_cluster_consensus_status",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_consensus_status_init, /* Plugin Init */
    nullptr,                   /* Plugin Check uninstall */
    i_s_common_deinit,         /* Plugin Deinit */
    i_s_plugin_version,        /* Plugin Version */
    nullptr,                   /* status variables */
    nullptr,                   /* system variables */
    nullptr,                   /* config options */
    0,                         /* flags */
};

struct st_mysql_plugin i_s_wesql_cluster_consensus_membership_change = {
    MYSQL_INFORMATION_SCHEMA_PLUGIN,
    &i_s_info,
    "WESQL_CLUSTER_CONSENSUS_MEMBERSHIP_CHANGE",
    plugin_author,
    "Consensus Replication wesql_cluster_consensus_membership_change",
    PLUGIN_LICENSE_GPL,
    wesql_cluster_consensus_membership_change_init, /* Plugin Init */
    nullptr,                   /* Plugin Check uninstall */
    i_s_common_deinit,         /* Plugin Deinit */
    i_s_plugin_version,        /* Plugin Version */
    nullptr,                   /* status variables */
    nullptr,                   /* system variables */
    nullptr,                   /* config options */
    0,                         /* flags */
};