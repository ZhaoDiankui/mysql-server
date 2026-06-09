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

const char *cluster_info_lines_on_objstore[] = {
    "number_of_lines", "index", "cluster_info", "cluster_learner_info"};

int ConsensusMeta::init() {
  DBUG_TRACE;

  Consensus_info_factory::init_consensus_repo_metadata();
  consensus_info = Consensus_info_factory::create_consensus_info();
  Consensus_info_factory::init_consensus_applier_repo_metadata();
  consensus_applier_info =
      Consensus_info_factory::create_consensus_applier_info();
  Consensus_info_factory::init_consensus_applier_worker_repo_metadata();
  if (!consensus_info || !consensus_applier_info) return 1;

  if (opt_serverless && opt_cluster_info_on_objectstore &&
      opt_server_id_on_objstore != nullptr &&
      opt_server_id_on_objstore[0] != '\0' && init_objstore_for_cluster_info())
    return 1;

  inited = true;
  return 0;
}

int ConsensusMeta::init_objstore_for_cluster_info() {
  DBUG_TRACE;
  std::string_view endpoint(
      opt_objstore_endpoint ? std::string_view(opt_objstore_endpoint) : "");
  std::string obj_error_msg;

  objstore::init_objstore_provider(opt_objstore_provider);

  cluster_info_objstore =
      objstore::create_object_store(std::string_view(opt_objstore_provider),
                                    std::string_view(opt_objstore_region),
                                    opt_objstore_endpoint ? &endpoint : nullptr,
                                    opt_objstore_use_https, obj_error_msg);
  if (!cluster_info_objstore) {
    LogErr(ERROR_LEVEL, ER_CONSENSUS_META_CREATE_OBJECT_STORE_ERROR,
           opt_objstore_provider, opt_objstore_region,
           std::string(endpoint).c_str(),
           opt_objstore_use_https ? "true" : "false",
           !obj_error_msg.empty() ? obj_error_msg.c_str() : "");
    return 1;
  }

  if (strcmp(opt_objstore_provider, "local") == 0) {
    cluster_info_objstore->create_bucket(std::string_view(opt_objstore_bucket));
  }

  cluster_info_bucket.append(opt_objstore_bucket);
  cluster_info_path.append(opt_repo_objstore_id);
  cluster_info_path.append("/");
  cluster_info_path.append(opt_branch_objstore_id);
  cluster_info_path.append("/");
  cluster_info_path.append("cluster_mgmt");
  cluster_info_path.append("/");
  cluster_info_path.append(opt_server_id_on_objstore);
  cluster_info_path.append("/");
  cluster_info_path.append("cluster_info");

  cluster_info_objstore_initied = true;

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

  if (cluster_info_objstore_initied && init_cluster_info_on_objstore()) {
    return -1;
  }

  return 0;
}

int ConsensusMeta::get_cluster_info(std::string &members_info,
                                    std::string &learners_info, uint64 &index) {
  if (get_consensus_info()->init_info()) return 1;

  if (cluster_info_objstore_initied) {
    if (init_cluster_info_on_objstore()) return 1;
    index = cluster_info_index;
  }

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

  if (cluster_info_objstore_initied) {
    if (set_index) cluster_info_index = index;
    return store_cluster_info_on_objstore();
  }

  return get_consensus_info()->flush_info(true, true);
}

int ConsensusMeta::init_cluster_info_on_objstore() {
  DBUG_TRACE;
  objstore::Status status;
  std::string members_info;
  std::string learners_info;
  size_t number_of_lines = 0;
  size_t expected_lines = sizeof(cluster_info_lines_on_objstore) /
                       sizeof(cluster_info_lines_on_objstore[0]);

  if (cluster_info_on_objstore_inited) return 0;

  if (read_cluster_info_on_objstore(members_info, learners_info,
                                    cluster_info_index, number_of_lines)) {
    return 1;
  }

  if (number_of_lines > CLUSTER_INFO)
    get_consensus_info()->set_cluster_info(members_info);
  if (number_of_lines > CLUSTER_LEARNER_INFO)
    get_consensus_info()->set_cluster_learner_info(learners_info);

  if (number_of_lines < expected_lines && store_cluster_info_on_objstore()) {
    return 1;
  }

  cluster_info_on_objstore_inited = true;

  return 0;
}

int ConsensusMeta::store_cluster_info_on_objstore() {
  DBUG_TRACE;

  std::ostringstream oss;
  size_t actual_lines = 3;
  std::string store_members_info = get_consensus_info()->get_cluster_info();
  std::string store_learners_info =
      get_consensus_info()->get_cluster_learner_info();

  if (!store_learners_info.empty()) actual_lines += 1;

  oss << actual_lines;
  oss << '\n';
  oss << cluster_info_index;
  oss << '\n';
  oss << store_members_info;
  oss << '\n';
  oss << store_learners_info;

  objstore::Status status = cluster_info_objstore->put_object(
      cluster_info_bucket, cluster_info_path, oss.str());
  if (!status.is_succ()) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_META_PUT_OBJECT_STORE_ERROR,
                 cluster_info_bucket.c_str(), cluster_info_path.c_str(),
                 status.error_message().data());
    return 1;
  }

  return 0;
}

int ConsensusMeta::read_cluster_info_on_objstore(std::string &members_info,
                                                 std::string &learners_info,
                                                 uint64 &index,
                                                 size_t &number_of_lines) {
  DBUG_TRACE;
  objstore::Status status;
  std::string cluster_info;

  status = cluster_info_objstore->get_object(cluster_info_bucket,
                                             cluster_info_path, cluster_info);
  if (!status.is_succ() &&
      status.error_code() != objstore::Errors::SE_NO_SUCH_KEY) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_META_GET_OBJECT_STORE_ERROR,
                 cluster_info_bucket.c_str(), cluster_info_path.c_str(),
                 status.error_message().data());
    return 1;
  }

  if (status.error_code() == objstore::Errors::SE_NO_SUCH_KEY) {
    number_of_lines = 0;
    return 0;
  }

  std::stringstream ss(cluster_info);
  std::vector<std::string> sections;
  std::string section;
  while (std::getline(ss, section)) {
    sections.push_back(section);
  }
  if (sections.size() <= NUMBER_OF_LINES) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_META_MALFORMED_CLUSTER_INFO_ERROR,
                 "lines less than 1");
    return 1;
  }

  number_of_lines =
      strtoul(sections[NUMBER_OF_LINES].c_str(), (char **)nullptr, 10);
  if (number_of_lines != sections.size()) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_META_MALFORMED_CLUSTER_INFO_ERROR,
                 "number_of_lines mismatch");
    return 1;
  }

  if (number_of_lines > INDEX)
    index = strtoll(sections[INDEX].c_str(), (char **)nullptr, 10);

  if (number_of_lines > CLUSTER_INFO) members_info = sections[CLUSTER_INFO];

  if (number_of_lines > CLUSTER_LEARNER_INFO)
    learners_info = sections[CLUSTER_LEARNER_INFO];

  return 0;
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