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

#include <stddef.h>

#include "consensus_state_process.h"
#include "observer_trans.h"
#include "plugin.h"
#include "rpl_consensus.h"
#include "system_variables.h"

#include "my_loglevel.h"
#include "sql/log.h"
#include "mysql/components/services/log_builtins.h"

#include "sql/debug_sync.h"
#include "sql/mysqld.h"
#include "sql/sql_lex.h"

int consensus_replication_trans_before_dml(Trans_param *param, int &out) {
  DBUG_TRACE;

  out = 0;

  if (opt_initialize) return 0;

  if (!plugin_is_consensus_replication_running()) return 0;

  /*
   The first check to be made is if the session binlog is active
   If it is not active, this query is not relevant for the plugin.
   */
  if (!param->trans_ctx_info.binlog_enabled) {
    return 0;
  }

  /*
    Cycle through all involved tables to assess if they all
    comply with the plugin runtime requirements. For now:
    - The table must be from a transactional engine
   */
  for (uint table = 0; out == 0 && table < param->number_of_tables; table++) {
    if (param->tables_info[table].db_type != DB_TYPE_INNODB
#ifdef WITH_SMARTENGINE
        && param->tables_info[table].db_type != DB_TYPE_SMARTENGINE
#endif
    ) {
#ifndef WESQL_TEST
      LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_RPL_NEEDS_TRANSACTIONAL_TABLE,
                   param->tables_info[table].table_name);
      out++;
#else
      LogPluginErr(WARNING_LEVEL, ER_CONSENSUS_RPL_NEEDS_TRANSACTIONAL_TABLE,
                   param->tables_info[table].table_name);
#endif
    }
  }

  return 0;
}

static bool disabled_command_for_consensus_replication(enum_sql_command cmd) {
  return (cmd == SQLCOM_START_GROUP_REPLICATION ||
          cmd == SQLCOM_STOP_GROUP_REPLICATION);
}

static bool disabled_command_on_consensus_follower(enum_sql_command cmd) {
  return (cmd == SQLCOM_CHANGE_MASTER);
}

static bool disabled_command_on_consensus_logger(enum_sql_command cmd) {
  return (cmd == SQLCOM_START_RAFT_REPLICATION) ||
         (cmd == SQLCOM_STOP_RAFT_REPLICATION) ||
         (cmd == SQLCOM_CHANGE_MASTER);
}

int consensus_replication_trans_begin(Trans_param *param [[maybe_unused]],
                                      int &out) {
  DBUG_TRACE;
  out = 0;

  if (opt_initialize || !current_thd) {
    return 0;
  }

  /* If the plugin is not running, before commit should return success. */
  if (!plugin_is_consensus_replication_running()) return 0;

  if (!rpl_consensus_is_ready()) {
    current_thd->consensus_context.binlog_disabled =
        (current_thd->variables.option_bits & OPTION_BIN_LOG);
    current_thd->variables.option_bits &= ~OPTION_BIN_LOG;
    return 0;
  }

  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_status_lock());

  if (rpl_consensus_is_ready() &&
      (!current_thd->slave_thread ||
       param->rpl_channel_type != CR_APPLIER_CHANNEL)) {
    if (disabled_command_for_consensus_replication(
            current_thd->lex->sql_command)) {
      out = ER_CONSENSUS_NOT_SUPPORT;
    } else if ((!rpl_consensus_is_leader() ||
                rpl_consensus_get_term() !=
                    consensus_state_process.get_current_term()) &&
               (current_thd->system_thread == SYSTEM_THREAD_EVENT_WORKER ||
                disabled_command_on_consensus_follower(
                    current_thd->lex->sql_command))) {
      out = ER_CONSENSUS_FOLLOWER_NOT_ALLOWED;
    }
  }

  if (!out && opt_cluster_log_type_instance &&
      disabled_command_on_consensus_logger(current_thd->lex->sql_command)) {
    out = ER_CONSENSUS_LOG_TYPE_NODE;
  }

  // store current term
  current_thd->consensus_context.consensus_term =
      consensus_state_process.get_current_term();

  mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());

  return 0;
}

int consensus_replication_trans_before_commit(Trans_param *) {
  DBUG_TRACE;
  return 0;
}

int consensus_replication_trans_before_rollback(Trans_param *) {
  DBUG_TRACE;
  return 0;
}

int consensus_replication_trans_after_commit(Trans_param *) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return success. */
  if (!plugin_is_consensus_replication_running()) return 0;

  if (!rpl_consensus_is_ready() && current_thd &&
      current_thd->consensus_context.binlog_disabled) {
    current_thd->variables.option_bits |= OPTION_BIN_LOG;
    current_thd->consensus_context.binlog_disabled = false;
  }

  return 0;
}

int consensus_replication_trans_after_rollback(Trans_param *param) {
  DBUG_TRACE;
  return consensus_replication_trans_after_commit(param);
}

Trans_observer cr_trans_observer = {
    sizeof(Trans_observer),

    consensus_replication_trans_before_dml,
    consensus_replication_trans_before_commit,
    consensus_replication_trans_before_rollback,
    consensus_replication_trans_after_commit,
    consensus_replication_trans_after_rollback,
    consensus_replication_trans_begin,
};
