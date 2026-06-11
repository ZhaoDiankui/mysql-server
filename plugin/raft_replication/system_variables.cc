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

#include "system_variables.h"
#include "consensus_log_manager.h"
#include "rpl_consensus.h"

#include "mysql/plugin.h"
#include "template_utils.h"

#include "mysqld_error.h"
#include "sql/log.h"
#include "sql/mysqld.h"
#include "sql/system_variables.h"

bool opt_old_show_timestamp = 0;
bool opt_reset_consensus_prefetch_cache = 0;
bool opt_consensus_checksum = 0;
bool opt_consensus_disable_election = 0;
bool opt_consensus_dynamic_easyindex = 1;
bool opt_consensus_easy_pool_size = 0;
char *opt_cluster_id;
bool opt_cluster_learner_node;
bool opt_cluster_log_type_instance;
char *opt_cluster_info;
bool opt_cluster_info_on_objectstore;
char *opt_cluster_purged_gtid;
ulonglong opt_cluster_current_term;
ulonglong opt_cluster_force_recover_index;
bool opt_cluster_rebuild;
bool opt_cluster_recover_from_backup;
bool opt_cluster_allow_no_valid_entry;
bool opt_cluster_archive_recovery;
char *opt_archive_log_index_name;
char *opt_archive_recovery_stop_datetime_str;
bool opt_cluster_force_change_meta;
bool opt_cluster_force_reset_meta;
ulonglong opt_consensus_log_cache_size;
bool opt_consensus_disable_fifo_cache;
ulonglong opt_consensus_prefetch_cache_size;
ulonglong opt_consensus_prefetch_window_size;
ulonglong opt_consensus_prefetch_wakeup_ratio;
ulonglong opt_consensus_max_log_size;
ulonglong opt_consensus_new_follower_threshold = 10000;
bool opt_consensus_large_trx;
ulonglong opt_consensus_large_event_split_size;
uint opt_consensus_send_timeout;
uint opt_consensus_learner_timeout;
bool opt_consensus_learner_pipelining = 0;
uint opt_consensus_configure_change_timeout = 60 * 1000;
uint opt_consensus_election_timeout;
uint opt_consensus_io_thread_cnt;
uint opt_consensus_worker_thread_cnt;
uint opt_consensus_heartbeat_thread_cnt;
ulong opt_consensus_max_packet_size;
ulong opt_consensus_pipelining_timeout = 1;
ulong opt_consensus_large_batch_ratio = 50;
ulonglong opt_consensus_max_delay_index;
ulonglong opt_consensus_min_delay_index;
bool opt_consensus_optimistic_heartbeat;
ulong opt_consensus_sync_follower_meta_interval = 1;
ulong opt_consensus_log_level;
ulonglong opt_consensus_start_index;
bool opt_mts_recover_use_index;
bool opt_cluster_force_single_mode;
bool opt_weak_consensus_mode;
bool opt_consensus_replicate_with_cache_log;
ulonglong opt_consensus_force_sync_epoch_diff = 0;
ulonglong opt_appliedindex_force_delay;
char *opt_consensus_flow_control = nullptr;
ulonglong opt_consensus_check_commit_index_interval = 0;
bool opt_consensus_auto_reset_match_index = 1;
bool opt_consensus_learner_heartbeat;
bool opt_consensus_auto_leader_transfer;
ulonglong opt_consensus_auto_leader_transfer_check_seconds;

static void fix_consensus_checksum(MYSQL_THD, SYS_VAR *, void *,
                                   const void *save) {
  opt_consensus_checksum = *static_cast<const bool *>(save);
  rpl_consensus_set_checksum_mode(opt_consensus_checksum);
}

static MYSQL_SYSVAR_BOOL(
    checksum, opt_consensus_checksum, PLUGIN_VAR_OPCMDARG,
    "Checksum when consensus receive log. Disabled by default.", nullptr,
    fix_consensus_checksum, false);

static void fix_consensus_disable_election(MYSQL_THD, SYS_VAR *, void *,
                                           const void *save) {
  opt_consensus_disable_election = *static_cast<const bool *>(save);
  rpl_consensus_set_disable_election(opt_consensus_disable_election,
                                     opt_consensus_disable_election);
}

static MYSQL_SYSVAR_BOOL(
    disable_election, opt_consensus_disable_election,
    PLUGIN_VAR_OPCMDARG, /* optional var */
    "Disable consensus election and stepdown. Disabled by default.", nullptr,
    fix_consensus_disable_election, false);

static void fix_consensus_dynamic_easyindex(MYSQL_THD, SYS_VAR *, void *,
                                            const void *save) {
  opt_consensus_dynamic_easyindex = *static_cast<const bool *>(save);
  rpl_consensus_set_enable_dynamic_easy_index(opt_consensus_dynamic_easyindex);
}

static MYSQL_SYSVAR_BOOL(dynamic_easyindex, opt_consensus_dynamic_easyindex,
                         PLUGIN_VAR_OPCMDARG,
                         "Enable dynamic easy addr cidx. Enabled by default.",
                         nullptr, fix_consensus_dynamic_easyindex, true);

static void handle_consensus_replicate_with_cache_log(MYSQL_THD, SYS_VAR *,
                                                      void *,
                                                      const void *save) {
  opt_consensus_replicate_with_cache_log = *static_cast<const bool *>(save);
  rpl_consensus_set_replicate_with_cache_log(
      opt_consensus_replicate_with_cache_log);
}

static MYSQL_SYSVAR_BOOL(with_cache_log, opt_consensus_replicate_with_cache_log,
                         PLUGIN_VAR_OPCMDARG,
                         "set server to replicate with cache log", nullptr,
                         handle_consensus_replicate_with_cache_log, false);

static void fix_consensus_force_sync_epoch_diff(MYSQL_THD, SYS_VAR *,
                                                void *,
                                                const void *save) {
  opt_consensus_force_sync_epoch_diff = *static_cast<const ulong *>(save);
  rpl_consensus_set_force_sync_epoch_diff(opt_consensus_force_sync_epoch_diff);
}

static MYSQL_SYSVAR_ULONGLONG(force_sync_epoch_diff,
                              opt_consensus_force_sync_epoch_diff,
                              PLUGIN_VAR_RQCMDARG,
                              "consensus forceSync epoch diff", nullptr,
                              fix_consensus_force_sync_epoch_diff, 0, 0,
                              10000000, 1);

static MYSQL_SYSVAR_STR(cluster_id, opt_cluster_id,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC |
                            PLUGIN_VAR_NOPERSIST | PLUGIN_VAR_READONLY,
                        "consensus cluster id", nullptr, nullptr, "");

static void fix_consensus_log_cache_size(MYSQL_THD, SYS_VAR *, void *,
                                         const void *save) {
  opt_consensus_log_cache_size = *static_cast<const ulonglong *>(save);
  consensus_log_manager.get_fifo_cache_manager()->set_max_log_cache_size(
      opt_consensus_log_cache_size);
}
static MYSQL_SYSVAR_ULONGLONG(log_cache_size, opt_consensus_log_cache_size,
                              PLUGIN_VAR_RQCMDARG, "Max cached logs size",
                              nullptr, fix_consensus_log_cache_size,
                              64 * 1024 * 1024, 1, ULLONG_MAX, 1);

static MYSQL_SYSVAR_BOOL(
    disable_fifo_cache, opt_consensus_disable_fifo_cache,
    PLUGIN_VAR_OPCMDARG, /* optional var */
    "disable consensus fifo cache (run with weak consensus mode)", nullptr,
    nullptr, false);

static void fix_consensus_prefetch_cache_size(MYSQL_THD, SYS_VAR *,
                                              void *, const void *save) {
  opt_consensus_prefetch_cache_size = *static_cast<const ulonglong *>(save);
  consensus_log_manager.get_prefetch_manager()->set_max_prefetch_cache_size(
      opt_consensus_prefetch_cache_size);
}

/*
 * opt_consensus_prefetch_cache_size >= 3 * opt_consensus_max_log_size
 * make sure at least 3 log entries can be stored in cache
 */
static int check_consensus_prefetch_cache_size(MYSQL_THD, SYS_VAR *, void *save,
                                               struct st_mysql_value *value) {
  longlong in_val;

  value->val_int(value, &in_val);
  if ((ulonglong)in_val < opt_consensus_max_log_size * 3) {
    my_message(ER_WRONG_VALUE_FOR_VAR,
               "The prefetch cache size cann't be smaller than "
               "3 times the max log entry size",
               MYF(0));
    return 1;
  }

  *static_cast<ulonglong *>(save) = in_val;
  return 0;
}

static MYSQL_SYSVAR_ULONGLONG(prefetch_cache_size,
                              opt_consensus_prefetch_cache_size,
                              PLUGIN_VAR_RQCMDARG, "Max cached logs size",
                              check_consensus_prefetch_cache_size,
                              fix_consensus_prefetch_cache_size,
                              64 * 1024 * 1024, 1, ULLONG_MAX, 1);

static void fix_consensus_prefetch_window_size(MYSQL_THD, SYS_VAR *,
                                               void *,
                                               const void *save) {
  opt_consensus_prefetch_window_size = *static_cast<const ulonglong *>(save);
  consensus_log_manager.get_prefetch_manager()->set_prefetch_window_size(
      opt_consensus_prefetch_window_size);
}

static MYSQL_SYSVAR_ULONGLONG(prefetch_window_size,
                              opt_consensus_prefetch_window_size,
                              PLUGIN_VAR_RQCMDARG, "prefetch window size",
                              nullptr, fix_consensus_prefetch_window_size, 10,
                              1, ULLONG_MAX, 1);

static void fix_consensus_prefetch_wakeup_ratio(MYSQL_THD, SYS_VAR *,
                                                void *,
                                                const void *save) {
  opt_consensus_prefetch_wakeup_ratio = *static_cast<const ulonglong *>(save);
  consensus_log_manager.get_prefetch_manager()->set_prefetch_wakeup_ratio(
      opt_consensus_prefetch_wakeup_ratio);
}

static MYSQL_SYSVAR_ULONGLONG(prefetch_wakeup_ratio,
                              opt_consensus_prefetch_wakeup_ratio,
                              PLUGIN_VAR_RQCMDARG, "prefetch wakeup ratio ",
                              nullptr, fix_consensus_prefetch_wakeup_ratio, 2,
                              1, ULLONG_MAX, 1);

static int check_consensus_max_log_size(MYSQL_THD, SYS_VAR *, void *save,
                                        struct st_mysql_value *value) {
  longlong in_val;
  value->val_int(value, &in_val);

  if (opt_consensus_prefetch_cache_size < in_val * 3) {
    my_message(ER_WRONG_VALUE_FOR_VAR,
               "The max log entry size cann't be larger than "
               "1/3 of the prefetch cache size",
               MYF(0));
    return 1;
  }

  *static_cast<ulonglong *>(save) = in_val;
  return 0;
}

static MYSQL_SYSVAR_ULONGLONG(max_log_size, opt_consensus_max_log_size,
                              PLUGIN_VAR_RQCMDARG,
                              "Max one log size. (default: 20M)",
                              check_consensus_max_log_size, nullptr,
                              20 * 1024 * 1024, 1, 1024 * 1024 * 1024, 1);

static void fix_consensus_new_follower_threshold(MYSQL_THD, SYS_VAR *, void *,
                                                 const void *save) {
  opt_consensus_new_follower_threshold = *static_cast<const ulonglong *>(save);
  rpl_consensus_set_new_follower_threshold(
      opt_consensus_new_follower_threshold);
}

static MYSQL_SYSVAR_ULONGLONG(
    new_follower_threshold, opt_consensus_new_follower_threshold,
    PLUGIN_VAR_RQCMDARG,
    "Max delay index to allow a learner becomes a follower", nullptr,
    fix_consensus_new_follower_threshold, 10000, 0, ULLONG_MAX, 1);

/* Large event or trx */
static MYSQL_SYSVAR_BOOL(large_trx, opt_consensus_large_trx,
                         PLUGIN_VAR_OPCMDARG, "consensus large trx or not",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_ULONGLONG(
    large_event_split_size, opt_consensus_large_event_split_size,
    PLUGIN_VAR_RQCMDARG,
    "split size for large event, dangerous to change this variable", nullptr,
    nullptr, 2 * 1024 * 1024, 1024, 20 * 1024 * 1024, 1);

/* Network */
static void fix_consensus_send_timeout(MYSQL_THD, SYS_VAR *, void *,
                                       const void *save) {
  opt_consensus_send_timeout = *static_cast<const uint *>(save);
  rpl_consensus_set_send_packet_timeout(opt_consensus_send_timeout);
}

static MYSQL_SYSVAR_UINT(send_timeout, opt_consensus_send_timeout,
                         PLUGIN_VAR_RQCMDARG, "Consensus send packet timeout",
                         nullptr, fix_consensus_send_timeout, 0, 0, 200000, 1);

static void fix_consensus_learner_timeout(MYSQL_THD, SYS_VAR *, void *,
                                          const void *save) {
  opt_consensus_learner_timeout = *static_cast<const uint *>(save);
  rpl_consensus_set_send_learner_timeout(opt_consensus_learner_timeout);
}

static MYSQL_SYSVAR_UINT(learner_timeout, opt_consensus_learner_timeout,
                         PLUGIN_VAR_RQCMDARG,
                         "Consensus learner connection timeout", nullptr,
                         fix_consensus_learner_timeout, 0, 0, 200000, 1);

static void fix_consensus_learner_pipelining(MYSQL_THD, SYS_VAR *,
                                             void *, const void *save) {
  opt_consensus_learner_pipelining = *static_cast<const bool *>(save);
  rpl_consensus_set_enable_learner_pipelining(opt_consensus_learner_pipelining);
}

static MYSQL_SYSVAR_BOOL(learner_pipelining, opt_consensus_learner_pipelining,
                         PLUGIN_VAR_OPCMDARG,
                         "enable pipelining send msg to learner", nullptr,
                         fix_consensus_learner_pipelining, false);

static void fix_consensus_configure_change_timeout(MYSQL_THD, SYS_VAR *,
                                                   void *,
                                                   const void *save) {
  opt_consensus_configure_change_timeout = *static_cast<const uint *>(save);
  rpl_consensus_set_configure_change_timeout(
      opt_consensus_configure_change_timeout);
}

static MYSQL_SYSVAR_UINT(
    configure_change_timeout, opt_consensus_configure_change_timeout,
    PLUGIN_VAR_RQCMDARG,
    "Consensus configure change timeout (ms). Default 1 min", nullptr,
    fix_consensus_configure_change_timeout, 60 * 1000, 0, 200000, 1);

static MYSQL_SYSVAR_UINT(election_timeout, opt_consensus_election_timeout,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Consensus election timeout", nullptr, nullptr, 5000,
                         20, 7200000, 1);

static MYSQL_SYSVAR_UINT(io_thread_cnt, opt_consensus_io_thread_cnt,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Number of consensus io thread", nullptr, nullptr, 4,
                         2, 128, 1);

static MYSQL_SYSVAR_UINT(worker_thread_cnt, opt_consensus_worker_thread_cnt,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Number of consensus worker thread", nullptr, nullptr,
                         4, 2, 128, 1);

static MYSQL_SYSVAR_UINT(heartbeat_thread_cnt,
                         opt_consensus_heartbeat_thread_cnt,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Number of consensus heartbeat thread", nullptr,
                         nullptr, 0, 0, 2, 1);

static void fix_consensus_max_packet_size(MYSQL_THD, SYS_VAR *, void *,
                                          const void *save) {
  opt_consensus_max_packet_size = *static_cast<const ulong *>(save);
  rpl_consensus_set_max_packet_size(opt_consensus_max_packet_size);
}

static MYSQL_SYSVAR_ULONG(max_packet_size, opt_consensus_max_packet_size,
                          PLUGIN_VAR_RQCMDARG,
                          "Max package size the consensus server send at once",
                          nullptr, fix_consensus_max_packet_size, 128 * 1024, 1,
                          1024 * 1024L * 1024L, 1);

static void fix_consensus_pipelining_timeout(MYSQL_THD, SYS_VAR *,
                                             void *, const void *save) {
  opt_consensus_pipelining_timeout = *static_cast<const ulong *>(save);
  rpl_consensus_set_pipelining_timeout(opt_consensus_pipelining_timeout);
}

static MYSQL_SYSVAR_ULONG(
    pipelining_timeout, opt_consensus_pipelining_timeout, PLUGIN_VAR_RQCMDARG,
    "the timeout the consensus server cache the log (milliseconds)", nullptr,
    fix_consensus_pipelining_timeout, 1, 0, 1024 * 1024L * 1024L, 1);

static void fix_consensus_large_batch_ratio(MYSQL_THD, SYS_VAR *, void *,
                                            const void *save) {
  opt_consensus_large_batch_ratio = *static_cast<const ulong *>(save);
  rpl_consensus_set_large_batch_ratio(opt_consensus_large_batch_ratio);
}

static MYSQL_SYSVAR_ULONG(large_batch_ratio, opt_consensus_large_batch_ratio,
                          PLUGIN_VAR_RQCMDARG,
                          "Large batch ratio of consensus server send at once",
                          nullptr, fix_consensus_large_batch_ratio, 50, 1, 1024,
                          1);

static void fix_consensus_max_delay_index(MYSQL_THD, SYS_VAR *, void *,
                                          const void *save) {
  opt_consensus_max_delay_index = *static_cast<const ulonglong *>(save);
  rpl_consensus_set_max_delay_index(opt_consensus_max_delay_index);
}

static MYSQL_SYSVAR_ULONGLONG(max_delay_index, opt_consensus_max_delay_index,
                              PLUGIN_VAR_RQCMDARG,
                              "Max index delay for pipeline log delivery",
                              nullptr, fix_consensus_max_delay_index, 50000, 1,
                              INT_MAX64, 1);

static void fix_consensus_min_delay_index(MYSQL_THD, SYS_VAR *, void *,
                                          const void *save) {
  opt_consensus_min_delay_index = *static_cast<const ulonglong *>(save);
  rpl_consensus_set_min_delay_index(opt_consensus_min_delay_index);
}

static MYSQL_SYSVAR_ULONGLONG(min_delay_index, opt_consensus_min_delay_index,
                              PLUGIN_VAR_RQCMDARG,
                              "Min index delay for pipeline log delivery",
                              nullptr, fix_consensus_min_delay_index, 5000, 1,
                              INT_MAX64, 1);

static void fix_consensus_optimistic_heartbeat(MYSQL_THD, SYS_VAR *,
                                               void *,
                                               const void *save) {
  opt_consensus_optimistic_heartbeat = *static_cast<const bool *>(save);
  rpl_consensus_set_optimistic_heartbeat(opt_consensus_optimistic_heartbeat);
}

static MYSQL_SYSVAR_BOOL(
    optimistic_heartbeat, opt_consensus_optimistic_heartbeat,
    PLUGIN_VAR_OPCMDARG,
    "whether to use optimistic heartbeat in consensus layer", nullptr,
    fix_consensus_optimistic_heartbeat, false);

static void fix_consensus_sync_follower_meta_interval(MYSQL_THD, SYS_VAR *,
                                                      void *,
                                                      const void *save) {
  opt_consensus_sync_follower_meta_interval = *static_cast<const ulong *>(save);
  rpl_consensus_set_sync_follower_meta_interval(
      opt_consensus_sync_follower_meta_interval);
}

static MYSQL_SYSVAR_ULONG(
    sync_follower_meta_interva, opt_consensus_sync_follower_meta_interval,
    PLUGIN_VAR_RQCMDARG,
    "Interval of leader sync follower's meta for learner source", nullptr,
    fix_consensus_sync_follower_meta_interval, 1, 1, 1024 * 1024L * 1024L, 1);

static MYSQL_SYSVAR_ULONGLONG(appliedindex_force_delay,
                              opt_appliedindex_force_delay, PLUGIN_VAR_RQCMDARG,
                              "force set a smaller appliedindex", nullptr,
                              nullptr, 0, 0, UINT64_MAX, 1);

static void fix_consensus_flow_control(MYSQL_THD, SYS_VAR *, void *var_ptr,
                                       const void *save) {
  const char *in_var = *static_cast<char *const *>(save);
  *static_cast<const char **>(var_ptr) = in_var;

  if (in_var == nullptr) return;

  // format: ip1:port1 fc1;ip2:port2 fc2...
  std::size_t current, previous = 0;
  std::string fcstr(in_var);
  std::vector<std::string> splits;
  current = fcstr.find(';');
  while (current != std::string::npos) {
    splits.push_back(fcstr.substr(previous, current - previous));
    previous = current + 1;
    current = fcstr.find(';', previous);
  }
  splits.push_back(fcstr.substr(previous, current - previous));
  if (splits.size() > 0) rpl_consensus_reset_flow_control();
  for (auto &kv : splits) {
    char addr[300];
    uint64 serverid;
    int64 fc;
    if (std::sscanf(kv.c_str(), "%s %ld", addr, &fc) == 2) {
      serverid = rpl_consensus_get_server_id(addr);
      rpl_consensus_set_flow_control(serverid, fc);
    }
  }
}

static MYSQL_SYSVAR_STR(
    flow_control, opt_consensus_flow_control,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "consensus flow control "
    "(<-1: no log send, -1: only send log during heartbeat, 0: no flow "
    "control, >0: [TODO] flow control).",
    nullptr, fix_consensus_flow_control, "");

static void fix_consensus_log_level(MYSQL_THD, SYS_VAR *, void *,
                                    const void *save) {
  // opt_consensus_log_level + 3 equal to easy log level
  opt_consensus_log_level = *static_cast<const ulong *>(save);
  rpl_consensus_set_alert_log_level(opt_consensus_log_level);
}

static const char *consensus_log_level_names[] = {
    "LOG_ERROR", "LOG_WARN", "LOG_INFO", "LOG_DEBUG", "LOG_TRACE", NullS};

/** Enumeration of consensus_log_level */
static TYPELIB cosensus_log_level_typelib = {
    array_elements(consensus_log_level_names) - 1,
    "consensus_log_level_typelib", consensus_log_level_names, nullptr};

static MYSQL_SYSVAR_ENUM(log_level, opt_consensus_log_level,
                         PLUGIN_VAR_RQCMDARG, "consensus log level", nullptr,
                         fix_consensus_log_level, 0,
                         &cosensus_log_level_typelib);

static MYSQL_SYSVAR_ULONGLONG(
    check_commit_index_interval, opt_consensus_check_commit_index_interval,
    PLUGIN_VAR_RQCMDARG, "check interval for slave calling checkCommitIndex",
    nullptr, nullptr, 1000, 0, INT_MAX64, 1);

static void handler_reset_consensus_prefetch_cache(MYSQL_THD, SYS_VAR *,
                                                   void *,
                                                   const void *save) {
  if (*(const bool *)save)
    consensus_log_manager.get_prefetch_manager()->reset_prefetch_cache();
}

static MYSQL_SYSVAR_BOOL(reset_prefetch_cache,
                         opt_reset_consensus_prefetch_cache,
                         PLUGIN_VAR_OPCMDARG, "reset consensus prefetch_cache",
                         nullptr, handler_reset_consensus_prefetch_cache,
                         false);

static void fix_consensus_auto_reset_match_index(MYSQL_THD, SYS_VAR *,
                                                 void *,
                                                 const void *save) {
  opt_consensus_auto_reset_match_index = *static_cast<const bool *>(save);
  rpl_consensus_set_enable_auto_reset_match_index(
      opt_consensus_auto_reset_match_index);
}

static MYSQL_SYSVAR_BOOL(
    auto_reset_match_index, opt_consensus_auto_reset_match_index,
    PLUGIN_VAR_OPCMDARG,
    "enable auto reset match index when consensus follower has fewer logs",
    nullptr, fix_consensus_auto_reset_match_index, true);

static void fix_consensus_learner_heartbeat(MYSQL_THD, SYS_VAR *, void *,
                                            const void *save) {
  opt_consensus_learner_heartbeat = *static_cast<const bool *>(save);
  rpl_consensus_set_enable_learner_heartbeat(opt_consensus_learner_heartbeat);
}

static MYSQL_SYSVAR_BOOL(learner_heartbeat, opt_consensus_learner_heartbeat,
                         PLUGIN_VAR_OPCMDARG,
                         "enable send heartbeat to learner", nullptr,
                         fix_consensus_learner_heartbeat, true);

static void fix_consensus_auto_leader_transfer(MYSQL_THD, SYS_VAR *,
                                               void *,
                                               const void *save) {
  opt_consensus_auto_leader_transfer = *static_cast<const bool *>(save);
  rpl_consensus_set_enable_auto_leader_transfer(
      opt_consensus_auto_leader_transfer);
}

static MYSQL_SYSVAR_BOOL(
    auto_leader_transfer, opt_consensus_auto_leader_transfer,
    PLUGIN_VAR_OPCMDARG,
    "whether to enable auto leader transfer in consensus layer", nullptr,
    fix_consensus_auto_leader_transfer, true);

static void fix_consensus_auto_leader_transfer_check_seconds(MYSQL_THD,
                                                             SYS_VAR *,
                                                             void *,
                                                             const void *save) {
  opt_consensus_auto_leader_transfer_check_seconds =
      *static_cast<const bool *>(save);
  rpl_consensus_set_auto_leader_transfer_check_seconds(
      opt_consensus_auto_leader_transfer_check_seconds);
}

static MYSQL_SYSVAR_ULONGLONG(
    auto_leader_transfer_check_seconds,
    opt_consensus_auto_leader_transfer_check_seconds, PLUGIN_VAR_RQCMDARG,
    "the interval between a leader check its health for a transfer", nullptr,
    fix_consensus_auto_leader_transfer_check_seconds, 60, 10, 300, 60);

static MYSQL_SYSVAR_BOOL(learner_node, opt_cluster_learner_node,
                         PLUGIN_VAR_OPCMDARG,
                         "Consensus cluster learner node type", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_BOOL(log_type_node, opt_cluster_log_type_instance,
                         PLUGIN_VAR_OPCMDARG,
                         "Consensus cluster log type instance", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_STR(cluster_info, opt_cluster_info,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC |
                            PLUGIN_VAR_NOPERSIST | PLUGIN_VAR_READONLY,
                        "Consensus cluster nodes ip-port information", nullptr,
                        nullptr, "");

static MYSQL_SYSVAR_BOOL(
    cluseter_info_on_objectstore, opt_cluster_info_on_objectstore,
    PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_NOPERSIST | PLUGIN_VAR_READONLY,
    "Use object store to store cluster info for consensus cluster", nullptr,
    nullptr, true);

static MYSQL_SYSVAR_STR(purged_gtid, opt_cluster_purged_gtid,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Consensus cluster set the purged gtid", nullptr,
                        nullptr, "");

static MYSQL_SYSVAR_ULONGLONG(
    current_term, opt_cluster_current_term,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_NOPERSIST | PLUGIN_VAR_READONLY,
    "Consensus cluster nodes current term  information", nullptr, nullptr, 0, 0,
    INT_MAX64, 1);

static MYSQL_SYSVAR_ULONGLONG(
    force_recover_index, opt_cluster_force_recover_index,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_NOPERSIST | PLUGIN_VAR_READONLY,
    "Consensus cluster restore from leader set apply index point", nullptr,
    nullptr, 0, 0, INT_MAX64, 1);

static MYSQL_SYSVAR_BOOL(recover_new_cluster, opt_cluster_rebuild,
                         PLUGIN_VAR_NOCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "recover new cluster from the snapshot of cloud "
                         "storage or the backup created with xtrabackup",
                         nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(recover_backup, opt_cluster_recover_from_backup,
                         PLUGIN_VAR_NOCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "recover from the backup created with xtrabackup or snapshot",
                         nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(allow_no_valid_entry, opt_cluster_allow_no_valid_entry,
                         PLUGIN_VAR_NOCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "allow consensus log without any valid entry", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_BOOL(archive_recovery, opt_cluster_archive_recovery,
                         PLUGIN_VAR_NOCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "archive recovery from the backup", nullptr, nullptr,
                         false);

static MYSQL_SYSVAR_BOOL(force_change_meta, opt_cluster_force_change_meta,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "Consensus cluster force to change meta", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_BOOL(force_reset_meta, opt_cluster_force_reset_meta,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "Consensus cluster force to reset meta", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_BOOL(force_single_mode, opt_cluster_force_single_mode,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_NOPERSIST |
                             PLUGIN_VAR_READONLY,
                         "Consensus cluster force to use single mode", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_BOOL(mts_recover_use_index, opt_mts_recover_use_index,
                         PLUGIN_VAR_OPCMDARG,
                         "Consensus cluster use idex to recover mts", nullptr,
                         nullptr, false);

static MYSQL_SYSVAR_ULONGLONG(start_index, opt_consensus_start_index,
                              PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_NOPERSIST |
                                  PLUGIN_VAR_READONLY,
                              "Consensus cluster start valid index", nullptr,
                              nullptr, 1, 0, INT_MAX64, 1);

static MYSQL_SYSVAR_STR(
    archive_log_bin_index, opt_archive_log_index_name,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC | PLUGIN_VAR_NOPERSIST |
        PLUGIN_VAR_READONLY,
    "File that holds the names for archived binary log files.", nullptr,
    nullptr, "");

static MYSQL_SYSVAR_STR(
    archive_recovery_stop_datetime, opt_archive_recovery_stop_datetime_str,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC | PLUGIN_VAR_NOPERSIST |
        PLUGIN_VAR_READONLY,
    "Stop recovery the archive binlog at first event having a datetime equal "
    "or "
    "posterior to the argument; the argument must be a date and time "
    "in the local time zone, in any format accepted by the MySQL server "
    "for DATETIME and TIMESTAMP types, for example: 2004-12-25 11:25:56 "
    "(you should probably use quotes for your shell to set it properly).",
    nullptr, nullptr, "");

SYS_VAR *consensus_replication_system_vars[] = {
    MYSQL_SYSVAR(checksum),
    MYSQL_SYSVAR(disable_election),
    MYSQL_SYSVAR(dynamic_easyindex),
    MYSQL_SYSVAR(with_cache_log),
    MYSQL_SYSVAR(force_sync_epoch_diff),
    MYSQL_SYSVAR(cluster_id),
    MYSQL_SYSVAR(log_cache_size),
    MYSQL_SYSVAR(disable_fifo_cache),
    MYSQL_SYSVAR(prefetch_cache_size),
    MYSQL_SYSVAR(prefetch_window_size),
    MYSQL_SYSVAR(prefetch_wakeup_ratio),
    MYSQL_SYSVAR(max_log_size),
    MYSQL_SYSVAR(new_follower_threshold),
    MYSQL_SYSVAR(large_trx),
    MYSQL_SYSVAR(large_event_split_size),
    MYSQL_SYSVAR(send_timeout),
    MYSQL_SYSVAR(learner_timeout),
    MYSQL_SYSVAR(learner_pipelining),
    MYSQL_SYSVAR(configure_change_timeout),
    MYSQL_SYSVAR(election_timeout),
    MYSQL_SYSVAR(io_thread_cnt),
    MYSQL_SYSVAR(worker_thread_cnt),
    MYSQL_SYSVAR(heartbeat_thread_cnt),
    MYSQL_SYSVAR(max_packet_size),
    MYSQL_SYSVAR(pipelining_timeout),
    MYSQL_SYSVAR(large_batch_ratio),
    MYSQL_SYSVAR(max_delay_index),
    MYSQL_SYSVAR(min_delay_index),
    MYSQL_SYSVAR(optimistic_heartbeat),
    MYSQL_SYSVAR(sync_follower_meta_interva),
    MYSQL_SYSVAR(appliedindex_force_delay),
    MYSQL_SYSVAR(flow_control),
    MYSQL_SYSVAR(log_level),
    MYSQL_SYSVAR(check_commit_index_interval),
    MYSQL_SYSVAR(reset_prefetch_cache),
    MYSQL_SYSVAR(auto_reset_match_index),
    MYSQL_SYSVAR(learner_heartbeat),
    MYSQL_SYSVAR(auto_leader_transfer),
    MYSQL_SYSVAR(auto_leader_transfer_check_seconds),
    MYSQL_SYSVAR(learner_node),
    MYSQL_SYSVAR(log_type_node),
    MYSQL_SYSVAR(cluster_info),
    MYSQL_SYSVAR(cluseter_info_on_objectstore),
    MYSQL_SYSVAR(purged_gtid),
    MYSQL_SYSVAR(current_term),
    MYSQL_SYSVAR(force_recover_index),
    MYSQL_SYSVAR(recover_new_cluster),
    MYSQL_SYSVAR(recover_backup),
    MYSQL_SYSVAR(allow_no_valid_entry),
    MYSQL_SYSVAR(force_change_meta),
    MYSQL_SYSVAR(force_reset_meta),
    MYSQL_SYSVAR(force_single_mode),
    MYSQL_SYSVAR(mts_recover_use_index),
    MYSQL_SYSVAR(start_index),
    MYSQL_SYSVAR(archive_recovery),
    MYSQL_SYSVAR(archive_log_bin_index),
    MYSQL_SYSVAR(archive_recovery_stop_datetime),
    nullptr,
};
