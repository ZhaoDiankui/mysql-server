/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2018, 2021, Alibaba and/or its affiliates.

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

#include <sstream>

#include "consensus_common.h"
#include "consensus_log_manager.h"
#include "consensus_state_process.h"
#include "rpl_consensus.h"  // ConsensusLogManager and alisql::Paxos
#include "system_variables.h"

#include "mysql/psi/mysql_file.h"
#include "sql/rpl_rli.h"

namespace im {

void collect_show_global_results(
    MEM_ROOT *mem_root, std::vector<Consensus_show_global_result *> &results) {
  std::vector<cluster_global_info> cgis;
  /* if node is not LEADER, global information is empty */
  if (rpl_consensus_get_cluster_global_info(&cgis, true) == 0) return;
  for (auto &ci : cgis) {
    Consensus_show_global_result *result =
        new (mem_root) Consensus_show_global_result();
    result->id = ci.server_id;
    result->ip_port.str =
        strmake_root(mem_root, ci.ip_port.c_str(), ci.ip_port.length());
    result->ip_port.length = ci.ip_port.length();
    result->match_index = ci.match_index;
    result->next_index = ci.next_index;
    result->role.str =
        strmake_root(mem_root, ci.role.c_str(), ci.role.length());
    result->role.length = ci.role.length();
    const char *res = nullptr;
    if (ci.force_sync)
      res = "Yes";
    else
      res = "No";
    result->force_sync.str = res;
    result->force_sync.length = strlen(res);
    result->election_weight = ci.election_weight;
    result->applied_index = ci.applied_index;
    result->learner_source = ci.learner_source;
    if (ci.pipelining)
      res = "Yes";
    else
      res = "No";
    result->pipelining.str = res;
    result->pipelining.length = strlen(res);
    if (ci.use_applied)
      res = "Yes";
    else
      res = "No";
    result->send_applied.str = res;
    result->send_applied.length = strlen(res);

    results.push_back(result);
  }
  return;
}

void collect_show_local_results(MEM_ROOT *mem_root,
                                Consensus_show_local_result *result) {
  const char *res = nullptr;
  cluster_local_info cli;
  rpl_consensus_get_cluster_local_info(&cli);
  result->id = cli.server_id;
  result->current_term = cli.current_term;
  result->current_leader.str =
      strmake_root(mem_root, cli.current_leader_ip_port.c_str(),
                   cli.current_leader_ip_port.length());
  result->current_leader.length = cli.current_leader_ip_port.length();
  result->commit_index = cli.commit_index;
  result->last_log_term = cli.last_log_term;
  result->last_log_index = cli.last_log_index;
  result->role.str =
      strmake_root(mem_root, cli.role.c_str(), cli.role.length());
  result->role.length = cli.role.length();
  result->vote_for = cli.voted_for;
  result->applied_index = cli.last_apply_index;

  Consensus_Log_System_Status rw_status = consensus_state_process.get_status();
  if (opt_cluster_log_type_instance) {
    res = "No";
  } else {
    if (rw_status == Consensus_Log_System_Status::BINLOG_WORKING)
      res = "Yes";
    else
      res = "No";
  }
  result->server_ready_for_rw.str = res;
  result->server_ready_for_rw.length = strlen(res);
  if (opt_cluster_log_type_instance)
    res = "Log";
  else
    res = "Normal";
  result->instance_type.str = res;
  result->instance_type.length = strlen(res);
  return;
}

void collect_show_logs_results(
    MEM_ROOT *mem_root, std::vector<Consensus_show_logs_result *> &results) {
  IO_CACHE *index_file;
  LOG_INFO cur;
  File file;
  char fname[FN_REFLEN];
  List<Item> field_list;
  size_t length;
  size_t cur_dir_len;

  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_status_lock());
  MYSQL_BIN_LOG *log = consensus_state_process.get_consensus_log();
  if (!log->is_open()) {
    mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
    my_error(ER_NO_BINARY_LOGGING, MYF(0));
    return;
  }

  mysql_mutex_lock(log->get_log_lock());
  log->lock_index();
  index_file = log->get_index_file();

  log->raw_get_current_log(&cur);           // dont take mutex
  mysql_mutex_unlock(log->get_log_lock());  // lockdep, OK
  mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());

  cur_dir_len = dirname_length(cur.log_file_name);

  reinit_io_cache(index_file, READ_CACHE, (my_off_t)0, 0, 0);

  /* The file ends with EOF or empty line */
  while ((length = my_b_gets(index_file, fname, sizeof(fname))) > 1) {
    size_t dir_len;
    int encrypted_header_size = 0;
    my_off_t file_length = 0;  // Length if open fails
    fname[--length] = '\0';    // remove the newline

    Consensus_show_logs_result *result =
        new (mem_root) Consensus_show_logs_result();
    dir_len = dirname_length(fname);
    length -= dir_len;
    result->log_name.str = strmake_root(mem_root, fname + dir_len, length);
    result->log_name.length = length;

    if (!(strncmp(fname + dir_len, cur.log_file_name + cur_dir_len, length))) {
      /* Encryption header size shall be accounted in the file_length */
      encrypted_header_size = cur.encrypted_header_size;
      file_length = cur.pos; /* The active log, use the active position */
      file_length = file_length + encrypted_header_size;
    } else {
      /* this is an old log, open it and find the size */
      if ((file = mysql_file_open(key_file_binlog, fname, O_RDONLY, MYF(0))) >=
          0) {
        file_length = (my_off_t)mysql_file_seek(file, 0L, MY_SEEK_END, MYF(0));
        mysql_file_close(file, MYF(0));
      }
    }
    result->file_size = file_length;

    uint64 start_index =
        consensus_log_manager.get_log_file_index()->get_start_index_of_file(
            std::string(fname));
    result->start_log_index = start_index;
    results.push_back(result);
  }
  log->unlock_index();
  return;
}

} /* namespace im */
