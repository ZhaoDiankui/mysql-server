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

#include <stdlib.h>
#include <sys/types.h>
#include <atomic>

#include <mysql/components/my_service.h>
#include <mysql/components/services/component_sys_var_service.h>
#include <mysql/components/services/group_replication_status_service.h>
#include "m_string.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_loglevel.h"
#include "my_sys.h"
#include "mysql/components/services/log_builtins.h"
#include "mysql/components/services/log_shared.h"
#include "mysql/plugin.h"
#include "mysql/plugin_consensus_replication.h"
#include "mysqld_error.h"  // ER_*

#include "sql/binlog.h"    /* mysql_bin_log.is_open() */
#include "sql/log.h"
#include "sql/log_event.h"           // MAX_MAX_ALLOWED_PACKET
#include "sql/mysqld.h"              // mysqld_port
#include "sql/mysqld_thd_manager.h"  // Global_THD_manager
#include "sql/replication.h"         // Trans_context_info
#include "sql/rpl_channel_credentials.h"
#include "sql/rpl_channel_service_interface.h"
#include "sql/rpl_gtid.h"     // Gtid_mode::lock
#include "sql/rpl_replica.h"  // report_host
#include "sql/sql_class.h"    // THD
#include "sql/sql_lex.h"
#include "sql/sql_plugin.h"  // plugin_unlock
#include "sql/sql_plugin_ref.h"
#include "sql/ssl_init_callback.h"

class THD;

extern ulong opt_mi_repository_id;
extern ulong opt_rli_repository_id;

namespace {
/**
  Static name of Consensus Replication plugin.
*/
LEX_CSTRING consensus_replication_plugin_name_str = {
    STRING_WITH_LEN("raft_replication")};
}  // namespace

void cr_get_server_startup_prerequirements(Trans_context_info &requirements, Binlog_context_info &infos) {
  requirements.binlog_enabled = opt_bin_log;
  requirements.binlog_format = global_system_variables.binlog_format;
  requirements.binlog_checksum_options = binlog_checksum_options;
  requirements.gtid_mode = global_gtid_mode.get();
  requirements.log_replica_updates = opt_log_replica_updates;
  requirements.transaction_write_set_extraction =
      global_system_variables.transaction_write_set_extraction;
  requirements.mi_repository_type = opt_mi_repository_id;
  requirements.rli_repository_type = opt_rli_repository_id;
  requirements.parallel_applier_type = mts_parallel_option;
  requirements.parallel_applier_workers = opt_mts_replica_parallel_workers;
  requirements.parallel_applier_preserve_commit_order =
      opt_replica_preserve_commit_order;
  requirements.lower_case_table_names = lower_case_table_names;
  requirements.default_table_encryption =
      global_system_variables.default_table_encryption;

  infos.binlog = &mysql_bin_log;
}

void init_consensus_context(THD *thd) {
  thd->consensus_context.consensus_index = 0;
  thd->consensus_context.consensus_term = 0;
  thd->consensus_context.consensus_error =
      Consensus_binlog_context_info::CSS_NONE;
  thd->consensus_context.consensus_replication_applier = false;
  thd->consensus_context.status_locked = false;
  thd->consensus_context.commit_locked= false;
}

void reset_consensus_context(THD *thd) {
  assert(!thd->consensus_context.status_locked);
  assert(!thd->consensus_context.commit_locked);
  thd->consensus_context.consensus_index = 0;
  thd->consensus_context.consensus_term = 0;
  thd->consensus_context.consensus_error =
      Consensus_binlog_context_info::CSS_NONE;
}

/*
  Consensus Replication plugin handler function accessors.
*/
bool is_consensus_replication_plugin_loaded() {
  bool result = false;

  plugin_ref plugin =
      my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                             MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    plugin_unlock(nullptr, plugin);
    result = true;
  }

  return result;
}


bool is_consensus_replication_enabled() {
  bool result = false;

  plugin_ref plugin =
      my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                             MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    st_mysql_consensus_replication *plugin_handle =
        (st_mysql_consensus_replication *)plugin_decl(plugin)->info;
    result = plugin_handle->is_running();
    plugin_unlock(nullptr, plugin);
  }

  return result;
}

bool is_consensus_replication_applier_running() {
  bool result = false;

  plugin_ref plugin =
      my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                             MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    st_mysql_consensus_replication *plugin_handle =
        (st_mysql_consensus_replication *)plugin_decl(plugin)->info;
    result = plugin_handle->is_applier_running();
    plugin_unlock(nullptr, plugin);
  }

  return result;
}

bool is_consensus_replication_log_mode() {
  bool result = false;

  plugin_ref plugin =
      my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                             MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    st_mysql_consensus_replication *plugin_handle =
        (st_mysql_consensus_replication *)plugin_decl(plugin)->info;
    result = plugin_handle->is_log_mode();
    plugin_unlock(nullptr, plugin);
  }

  return result;
}

bool is_consensus_replication_state_leader(uint64 &term) {
  bool result = false;

  plugin_ref plugin =
      my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                             MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    st_mysql_consensus_replication *plugin_handle =
        (st_mysql_consensus_replication *)plugin_decl(plugin)->info;
    result = plugin_handle->is_state_leader(term);
    plugin_unlock(nullptr, plugin);
  }

  return result;
}

bool consensus_replication_show_log_events(THD *thd) {
  bool result = false;
  plugin_ref plugin = nullptr;

  plugin = my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                                  MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    st_mysql_consensus_replication *plugin_handle =
        (st_mysql_consensus_replication *)plugin_decl(plugin)->info;
    result = plugin_handle->show_log_events(thd);
    plugin_unlock(nullptr, plugin);
  } else {
    LogErr(ERROR_LEVEL, ER_CONSENSUS_REPLICATION_PLUGIN_NOT_INSTALLED);
    my_error(ER_CONSENSUS_CONFIG_BAD, MYF(0));
  }
  return result;
}

bool consensus_replication_show_logs(THD *thd) {
  bool result = false;
  plugin_ref plugin = nullptr;

  plugin = my_plugin_lock_by_name(nullptr, consensus_replication_plugin_name_str,
                                  MYSQL_REPLICATION_PLUGIN);
  if (plugin != nullptr) {
    st_mysql_consensus_replication *plugin_handle =
        (st_mysql_consensus_replication *)plugin_decl(plugin)->info;
    result = plugin_handle->show_logs(thd);
    plugin_unlock(nullptr, plugin);
  } else {
    LogErr(ERROR_LEVEL, ER_CONSENSUS_REPLICATION_PLUGIN_NOT_INSTALLED);
    my_error(ER_CONSENSUS_CONFIG_BAD, MYF(0));
  }
  return result;
}