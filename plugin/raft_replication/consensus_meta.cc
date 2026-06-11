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

#include "consensus_meta.h"
#include "consensus_state_process.h"
#include "plugin.h"
#include "plugin_psi.h"
#include "system_variables.h"

#include "mysqld_error.h"
#include "sql/mysqld.h"

#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/log.h"

#include "sql/consensus/consensus_info_factory.h"
#include "sql/rpl_info_factory.h"

ConsensusMeta consensus_meta;

enum cluster_info_section_enum {
  NUMBER_OF_LINES = 0,
  INDEX,
  CLUSTER_INFO,
  CLUSTER_LEARNER_INFO
};

int ConsensusMeta::init() {
  DBUG_TRACE;

  Consensus_info_factory::init_consensus_repo_metadata();
  consensus_info = Consensus_info_factory::create_consensus_info();
  Consensus_info_factory::init_consensus_applier_repo_metadata();
  consensus_applier_info =
      Consensus_info_factory::create_consensus_applier_info();
  Consensus_info_factory::init_consensus_applier_worker_repo_metadata();
  if (!consensus_info || !consensus_applier_info) return 1;

  inited = true;
  return 0;
}

int ConsensusMeta::init_consensus_info() {
  DBUG_TRACE;
  if (consensus_info->init_info()) {
    return -1;
  }

  if (consensus_applier_info->init_info()) {
    return -1;
  }

  return 0;
}

int ConsensusMeta::get_cluster_info(std::string &members_info,
                                    std::string &learners_info, uint64 &index) {
  if (get_consensus_info()->init_info()) return 1;

  members_info = get_consensus_info()->get_cluster_info();
  learners_info = get_consensus_info()->get_cluster_learner_info();

  return 0;
}

int ConsensusMeta::set_cluster_info(bool set_members,
                                    const std::string &members_info,
                                    bool set_learners,
                                    const std::string &learners_info,
                                    bool set_index, uint64 index) {
  if (set_members) get_consensus_info()->set_cluster_info(members_info);
  if (set_learners)
    get_consensus_info()->set_cluster_learner_info(learners_info);

  return get_consensus_info()->flush_info(true, true);
}

int ConsensusMeta::change_meta_if_needed() {
  DBUG_TRACE;
  Consensus_info *consensus_info = get_consensus_info();
  if (opt_cluster_force_change_meta || opt_cluster_force_reset_meta) {
    if (opt_cluster_id)
      consensus_info->set_cluster_id(std::string(opt_cluster_id));

    if (opt_cluster_current_term)
      consensus_info->set_current_term(opt_cluster_current_term);
    else if (opt_cluster_force_reset_meta)
      consensus_info->set_current_term(1);

    if (opt_cluster_force_recover_index || opt_cluster_force_reset_meta) {
      consensus_info->set_recover_status(
          Consensus_Log_System_Status::RELAY_LOG_WORKING);
      consensus_info->set_last_leader_term(0);
      consensus_info->set_start_apply_index(opt_cluster_force_recover_index);
    }

    if (!opt_cluster_info) {
      LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_CHANGE_META_ERROR,
                   "raft_replication_cluster_info "
                   "must be set when the server is "
                   "running with --raft-replication-force-change-meta");
      return -1;
    }
    set_cluster_info(true, std::string(opt_cluster_info), false,
                     std::string(""), false, 0);
    consensus_info->flush_info(true, true);

    LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_CHANGE_META_LOG);
  }

  return 0;
}

int ConsensusMeta::update_consensus_info() {
  int error = 0;
  DBUG_TRACE;

  if (!opt_initialize) {
    error = change_meta_if_needed();
  } else {
    Consensus_info *consensus_info = get_consensus_info();

    if (opt_cluster_id) {
      consensus_info->set_cluster_id(std::string(opt_cluster_id));
      consensus_info->flush_info(true, true);
    }

    if (!opt_cluster_info) {
      LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_CHANGE_META_ERROR,
                   "raft_replication_cluster_info "
                   "must be set when the server is "
                   "running with --initialize(-insecure)");
      return -1;
    }

    if (!opt_cluster_learner_node) {
      set_cluster_info(true, std::string(opt_cluster_info), false,
                       std::string(""), true, 1);
    } else {
      set_cluster_info(false, std::string(""), true,
                       std::string(opt_cluster_info), true, 1);
    }
  }

  return error;
}

int ConsensusMeta::set_start_apply_index_if_need(uint64 consensus_index) {
  DBUG_TRACE;

  if (opt_cluster_log_type_instance) return 0;

  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_status_lock());
  if (!already_set_start_index &&
      consensus_state_process.get_status() ==
          Consensus_Log_System_Status::BINLOG_WORKING) {
    consensus_info->set_start_apply_index(consensus_index);
    if (consensus_info->flush_info(true, true)) {
      mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
      return 1;
    }
    already_set_start_index = true;
  }
  mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());

  return 0;
}

int ConsensusMeta::set_start_apply_term_if_need(uint64 consensus_term) {
  DBUG_TRACE;

  if (opt_cluster_log_type_instance) return 0;

  mysql_rwlock_rdlock(consensus_state_process.get_consensuslog_status_lock());
  if (!already_set_start_term &&
      consensus_state_process.get_status() ==
          Consensus_Log_System_Status::BINLOG_WORKING) {
    consensus_info->set_last_leader_term(consensus_term);
    if (consensus_info->flush_info(true, true)) {
      mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());
      return 1;
    }
    already_set_start_term = true;
  }
  mysql_rwlock_unlock(consensus_state_process.get_consensuslog_status_lock());

  return 0;
}

int ConsensusMeta::cleanup() {
  DBUG_TRACE;

  if (inited) {
    consensus_info->end_info();
    delete consensus_info;
    delete consensus_applier_info;
  }
  return 0;
}