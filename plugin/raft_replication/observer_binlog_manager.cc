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

#include "consensus_applier.h"
#include "consensus_binlog.h"
#include "consensus_binlog_recovery.h"
#include "consensus_log_manager.h"
#include "consensus_state_process.h"
#include "observer_binlog_manager.h"
#include "plugin.h"
#include "system_variables.h"

#include "my_inttypes.h"
#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/debug_sync.h"  // DEBUG_SYNC
#include "sql/log.h"
#include "sql/rpl_commit_stage_manager.h"
#include "sql/sql_lex.h"

static int consensus_binlog_manager_binlog_recovery(
    Binlog_manager_param *param) {
  DBUG_TRACE;

  /* If the plugin is not enabled, return success. */
  if (!opt_bin_log) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  if (consensus_build_log_file_index(param->binlog)) return 1;

  assert(param->binlog == consensus_state_process.get_binlog());

  return opt_initialize
             ? 0
             : (consistent_recovery_consensus_recovery
                    ? consensus_binlog_recovery(
                          param->binlog, true,
                          (uint64)consistent_recovery_snasphot_end_consensus_index,
                          consistent_recovery_consensus_truncated_end_binlog,
                          &consistent_recovery_consensus_truncated_end_position)
                    : consensus_binlog_recovery(param->binlog, false, 0,
                                                nullptr, nullptr));
}

static int consensus_binlog_manager_after_binlog_recovery(
    Binlog_manager_param *param) {
  DBUG_TRACE;
  int error = 0;

  /* If the binlog is not enabled, return success. */
  if (!opt_bin_log) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  assert(param->binlog == consensus_state_process.get_binlog());

  /* Open a new binlog file if not exits */
  std::vector<std::string> binlog_file_list;
  consensus_log_manager.get_log_file_index()->get_log_file_list(
      binlog_file_list);

  mysql_mutex_t *log_lock = param->binlog->get_log_lock();
  mysql_mutex_lock(log_lock);

  if (binlog_file_list.empty()) {
    if (opt_serverless && opt_cluster_log_type_instance) {
      /* Use the next index of the last persisted binlog as a starting index */
      consensus_log_manager.set_current_index(
          consistent_recovery_snasphot_end_consensus_index + 1);
    }

    if (param->binlog->open_binlog(opt_bin_logname, nullptr, max_binlog_size,
                                   false, true /*need_lock_index=true*/,
                                   true /*need_sid_lock=true*/, nullptr)) {
      error = 1;
    }

    consensus_log_manager.set_start_without_log(true);
    consensus_log_manager.set_sync_index_if_greater(
        consensus_log_manager.get_current_index() - 1);
  } else {
    if (param->binlog->open_exist_consensus_binlog(
            opt_bin_logname, max_binlog_size, false,
            true /*need_lock_index=true*/)) {
      error = 1;
    }
  }

  mysql_mutex_unlock(log_lock);

  return error;
}

static int consensus_binlog_manager_gtid_recovery(Binlog_manager_param *) {
  /* gtid recovery in hook server_state.after_engine_recovery */
  return 0;
}

static int consensus_binlog_manager_new_file(
    Binlog_manager_param *param, const char *log_file_name,
    bool null_created_arg, bool need_sid_lock,
    Format_description_log_event *extra_description_event [[maybe_unused]],
    bool &write_file_name_to_index_file) {
  DBUG_TRACE;
  int error = 0;
  uint32 tv_sec = 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  mysql_mutex_assert_owner(param->binlog->get_log_lock());

  if (param->binlog->is_consensus_write)
    tv_sec = consensus_log_manager.get_event_timestamp();

  error = param->binlog->init_consensus_binlog(
      null_created_arg, need_sid_lock, write_file_name_to_index_file,
      consensus_log_manager.get_current_index(), tv_sec);

  if (!error) {
    if (!param->binlog->is_consensus_write)
      consensus_log_manager.set_next_event_rotate_flag(true);

    std::string file_name(log_file_name);
    consensus_log_manager.get_log_file_index()->add_to_index_list(
        consensus_log_manager.get_current_index(), tv_sec, file_name);
  }

  return error;
}

static int consensus_binlog_manager_after_purge_file(
    Binlog_manager_param *param, const char *log_file_name) {
  DBUG_TRACE;
  int error = 0;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  if (!param->binlog->is_relay_log) {
    std::string file_name = std::string(log_file_name);
    if (param->binlog == consensus_state_process.get_consensus_log())
      consensus_log_manager.get_log_file_index()->truncate_before(file_name);

    global_sid_lock->wrlock();
    error = param->binlog->consensus_init_gtid_sets(
        nullptr, const_cast<Gtid_set *>(gtid_state->get_lost_gtids()),
        opt_source_verify_checksum, false);
    global_sid_lock->unlock();
  }

  return error;
}

static int consensus_binlog_manager_before_flush(Binlog_manager_param *param) {
  DBUG_TRACE;
  int error = 0;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  THD *thd = param->thd;

  /* The logger node is not allowed to write to consensus log. */
  if (opt_cluster_log_type_instance) {
    thd->mark_transaction_to_rollback(true);
    thd->commit_error = THD::CE_COMMIT_ERROR;
    my_error(ER_BINLOG_LOGGING_IMPOSSIBLE, MYF(0),
             "Consensus Logger disabled write binlog");
    return 1;
  }

  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_status_lock());

  if (thd->consensus_context.consensus_term == 0)
    thd->consensus_context.consensus_term =
        consensus_state_process.get_current_term();

  /* Check server status */
  mysql_mutex_lock(consensus_state_process.get_log_term_lock());
  if (consensus_state_process.get_status() !=
          Consensus_Log_System_Status::BINLOG_WORKING ||
      thd->consensus_context.consensus_term !=
          consensus_state_process.get_current_term() ||
      rpl_consensus_log_get_term() !=
          consensus_state_process.get_current_term() ||
      rpl_consensus_get_term() != consensus_state_process.get_current_term()) {
    error = 1;
  }
  mysql_mutex_unlock(consensus_state_process.get_log_term_lock());

  if (error) {
    mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
    thd->mark_transaction_to_rollback(true);
    thd->commit_error = THD::CE_COMMIT_ERROR;
    my_error(ER_BINLOG_LOGGING_IMPOSSIBLE, MYF(0), "Consensus Not Leader");
  } else {
    // consensuslog_status_lock got
    thd->consensus_context.status_locked = true;
  }

  return error;
}

static int consensus_binlog_manager_write_transaction(
    Binlog_manager_param *param, Gtid_log_event *gtid_event,
    binlog_cache_data *cache_data, bool have_checksum) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  return write_binlog_cache_to_consensus_log(
      param->thd, param->binlog, *gtid_event, cache_data, have_checksum);
}

static int consensus_binlog_manager_after_write(Binlog_manager_param *) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  rpl_consensus_write_log_cache_done();

  DBUG_EXECUTE_IF("consensus_replication_crash_after_write_cache",
                  { DBUG_SUICIDE(); });

  return 0;
}

static int consensus_binlog_manager_after_flush(Binlog_manager_param *param,
                                                bool &delay_update_binlog_pos) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  delay_update_binlog_pos = true;
  return 0;
}

static int consensus_binlog_manager_after_sync(Binlog_manager_param *param,
                                               bool &delay_update_binlog_pos) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  delay_update_binlog_pos = true;
  return 0;
}

static int consensus_binlog_manager_after_enrolling_stage(
    Binlog_manager_param *param, int stage) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  /* No action is required for other stages */
  if (Commit_stage_manager::StageID(stage) !=
      Commit_stage_manager::COMMIT_STAGE)
    return 0;

  DEBUG_SYNC(param->thd, "consensus_replication_after_sync");
  DBUG_EXECUTE_IF("consensus_replication_crash_after_sync",
                  { DBUG_SUICIDE(); });

  uint64 group_max_log_index = 0;
  for (THD *head = param->thd; head; head = head->next_to_commit) {
    if (head->consensus_context.consensus_index > group_max_log_index) {
      group_max_log_index = head->consensus_context.consensus_index;
    }
  }

  /* The group log may potentially be truncated. */
  mysql_rwlock_rdlock(consensus_log_manager.get_consensuslog_truncate_lock());
  if (group_max_log_index > 0 &&
      group_max_log_index < consensus_log_manager.get_current_index()) {
    consensus_log_manager.set_sync_index_if_greater(group_max_log_index);
  }
  mysql_rwlock_unlock(consensus_log_manager.get_consensuslog_truncate_lock());

  /* The group log may potentially be truncated. If not,
   * group_max_log_index is not more than sync_index. */
  if (group_max_log_index > 0 &&
      group_max_log_index <= consensus_log_manager.get_sync_index()) {
    rpl_consensus_write_log_done(group_max_log_index);
  }

  for (THD *head = param->thd; head; head = head->next_to_commit) {
    /* whether consensus layer allow to commit */
    if (head->consensus_context.consensus_error ==
        Consensus_binlog_context_info::CSS_NONE) {
      consensus_before_commit(head);
    }

    /* updates the binlog end position */
    if (head->consensus_context.consensus_error ==
        Consensus_binlog_context_info::CSS_NONE) {
      const char *binlog_file = nullptr;
      my_off_t pos = 0;

      head->get_trans_fixed_pos(&binlog_file, &pos);
      if (binlog_file != nullptr && pos > 0) {
        binlog_update_end_pos(param->binlog, binlog_file, pos);
      }
    }
  }

  /* after waitCommitIndexUpdate */
  DEBUG_SYNC(param->thd, "consensus_replication_after_consensus_commit");
  DBUG_EXECUTE_IF("consensus_replication_crash_after_commit",
                  { DBUG_SUICIDE(); });

  // Release consensus log lock for leader
  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_commit_lock());
  param->thd->consensus_context.commit_locked = true;
  assert(param->thd->consensus_context.status_locked);
  mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
  param->thd->consensus_context.status_locked = false;

  return 0;
}

static int consensus_binlog_manager_before_finish_in_engines(
    Binlog_manager_param *param, bool finish_commit) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  THD *thd = param->thd;

  // Consensus log lock has not released
  if (finish_commit && thd->consensus_context.status_locked) {
    mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
    thd->consensus_context.status_locked = false;
  }

  if (thd->commit_error != THD::CE_COMMIT_ERROR &&
      thd->consensus_context.consensus_error !=
          Consensus_binlog_context_info::CSS_NONE) {
    thd->mark_transaction_to_rollback(true);
    thd->commit_error = THD::CE_COMMIT_ERROR;

    if (thd->consensus_context.consensus_error ==
        Consensus_binlog_context_info::CSS_LEADERSHIP_CHANGE)
      my_error(ER_CONSENSUS_LEADERSHIP_CHANGE, MYF(0));
    else if (thd->consensus_context.consensus_error ==
             Consensus_binlog_context_info::CSS_LOG_TOO_LARGE)
      my_error(ER_CONSENSUS_LOG_TOO_LARGE, MYF(0));
    else if (thd->consensus_context.consensus_error ==
             Consensus_binlog_context_info::CSS_SHUTDOWN)
      my_error(ER_SERVER_SHUTDOWN, MYF(0));
    else
      my_error(ER_CONSENSUS_OTHER_ERROR, MYF(0));
  }

  return (thd->consensus_context.consensus_error !=
          Consensus_binlog_context_info::CSS_NONE);
}

static int consensus_binlog_manager_after_finish_commit(
    Binlog_manager_param *param) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  THD *thd = param->thd;

  if (thd->consensus_context.commit_locked) {
    mysql_rwlock_unlock(consensus_state_process.get_consensuslog_commit_lock());
    thd->consensus_context.commit_locked = false;
  }

  uint64 commit_index = rpl_consensus_get_commit_index();

  update_consensus_applied_index(commit_index);

  return 0;
}

static int consensus_binlog_manager_before_rotate(Binlog_manager_param *param,
                                                  bool &do_rotate) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  THD *thd = param->thd;

  /* Check server status */
  assert(!thd->consensus_context.status_locked);
  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_status_lock());
  if (consensus_state_process.get_status() !=
      Consensus_Log_System_Status::BINLOG_WORKING) {
    do_rotate = false;
  }

  /* Check consensus log rotate status */
  if (do_rotate){
    mysql_rwlock_rdlock(consensus_log_manager.get_consensuslog_rotate_lock());
    if (!consensus_log_manager.get_enable_rotate()) {
      do_rotate = false;
      mysql_rwlock_unlock(consensus_log_manager.get_consensuslog_rotate_lock());
    }
  }

  if (!do_rotate) {
    mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
  } else {
    thd->consensus_context.status_locked = true;
  }

  return 0;
}

static int consensus_binlog_manager_after_rotate(Binlog_manager_param *param) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  THD *thd = param->thd;

  if (thd->consensus_context.status_locked) {
    mysql_rwlock_unlock(consensus_log_manager.get_consensuslog_rotate_lock());
    mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
    thd->consensus_context.status_locked = false;
  }

  return 0;
}

static int consensus_binlog_manager_rotate_and_purge(
    Binlog_manager_param *param, bool force_rotate) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  return rotate_consensus_log(param->thd, force_rotate);
}

static int consensus_binlog_manager_reencrypt_logs(Binlog_manager_param *) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  return reencrypt_consensus_logs();
}

static int consensus_binlog_manager_purge_logs(Binlog_manager_param *,
                                               ulong purge_time,
                                               ulong purge_size,
                                               const char *to_log,
                                               bool auto_purge) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  /* If the plugin is not running, return failed. */
  if (!plugin_is_consensus_replication_running()) return 1;

  return purge_consensus_logs_on_conditions(purge_time, purge_size, to_log,
                                            auto_purge);
}

/* If log_pos is zero, scan to the end. */
static int consensus_binlog_manager_get_unique_index_from_pos(
    Binlog_manager_param *, const char *log_file_name, my_off_t log_pos,
    uint64 &unique_index) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  // If the plugin is not running, return failed.
  if (!plugin_is_consensus_replication_running()) return 1;

  uint64 next_index = consensus_log_manager.get_next_index_from_position(
      log_file_name, log_pos, true);

  unique_index = next_index > 0 ? next_index - 1 : 0;

  return (next_index == 0);
}

static int consensus_binlog_manager_get_pos_from_unique_index(Binlog_manager_param *,
                                                       uint64 unique_index,
                                                       char *log_file_name,
                                                       my_off_t &log_pos) {
  DBUG_TRACE;

  if (opt_initialize) return 0;

  // If the plugin is not running, return failed.
  if (!plugin_is_consensus_replication_running()) return 1;

  return consensus_log_manager.get_log_end_position(unique_index, true,
                                                    log_file_name, &log_pos);
}

Binlog_manager_observer cr_binlog_mananger_observer = {
    sizeof(Binlog_manager_observer),

    consensus_binlog_manager_binlog_recovery,
    consensus_binlog_manager_after_binlog_recovery,
    consensus_binlog_manager_gtid_recovery,
    consensus_binlog_manager_new_file,
    consensus_binlog_manager_after_purge_file,
    consensus_binlog_manager_before_flush,
    consensus_binlog_manager_write_transaction,
    consensus_binlog_manager_after_write,
    consensus_binlog_manager_after_flush,
    consensus_binlog_manager_after_sync,
    consensus_binlog_manager_after_enrolling_stage,
    consensus_binlog_manager_before_finish_in_engines,
    consensus_binlog_manager_after_finish_commit,
    consensus_binlog_manager_before_rotate,
    consensus_binlog_manager_after_rotate,
    consensus_binlog_manager_rotate_and_purge,
    consensus_binlog_manager_purge_logs,
    consensus_binlog_manager_reencrypt_logs,
    consensus_binlog_manager_get_unique_index_from_pos,
    consensus_binlog_manager_get_pos_from_unique_index,
};
