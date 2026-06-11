/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited.
   Portions Copyright (c) 2018, 2021, Alibaba and/or its affiliates.
   Portions Copyright (c) 2009, 2023, Oracle and/or its affiliates.

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


#include "sql/consensus/consensus_info.h"

#include "mysqld_error.h"
#include "sql/log.h"
#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"

#include "sql/current_thd.h"
#include "sql/handler.h"
#include "sql/log.h"
#include "sql/mysqld.h"

const char *info_consensus_fields[] = {
    "number_of_lines",      "vote_for",         "current_term",
    "recover_status",       "last_leader_term", "start_apply_index",
    "cluster_id",           "cluster_info",     "cluster_learner_info",
    "cluster_recover_index"};

Consensus_info::Consensus_info(
#ifdef HAVE_PSI_INTERFACE
    PSI_mutex_key *param_key_info_run_lock,
    PSI_mutex_key *param_key_info_data_lock,
    PSI_mutex_key *param_key_info_sleep_lock,
    PSI_mutex_key *param_key_info_thd_lock,
    PSI_mutex_key *param_key_info_data_cond,
    PSI_mutex_key *param_key_info_start_cond,
    PSI_mutex_key *param_key_info_stop_cond,
    PSI_mutex_key *param_key_info_sleep_cond
#endif
    )
    : Rpl_info("Consensus info"
#ifdef HAVE_PSI_INTERFACE
               ,
               param_key_info_run_lock, param_key_info_data_lock,
               param_key_info_sleep_lock, param_key_info_thd_lock,
               param_key_info_data_cond, param_key_info_start_cond,
               param_key_info_stop_cond, param_key_info_sleep_cond
#endif
               ,
               0, ""),
      vote_for(0),
      current_term(1),
      recover_status(0),
      last_leader_term(0),
      start_apply_index(0),
      cluster_id(""),
      cluster_info(""),
      cluster_learner_info(""),
      cluster_recover_index(0) {
}

/**
Creates or reads information from the repository, initializing the
Consensus_info.
*/
int Consensus_info::init_info() {
  enum_return_check check_return = ERROR_CHECKING_REPOSITORY;
  DBUG_TRACE;

  if (inited) return 0;

  mysql_mutex_init(key_LOCK_consensus_info, &LOCK_consensus_info,
                   MY_MUTEX_INIT_FAST);

  if ((check_return = check_info()) == ERROR_CHECKING_REPOSITORY) goto err;

  if (handler->init_info()) goto err;

  if (check_return != REPOSITORY_DOES_NOT_EXIST) {
    if (read_info(handler)) goto err;
  }

  inited = 1;
  if (flush_info(true, true)) goto err;

  return 0;
err:
  handler->end_info();
  inited = 0;
  LogErr(ERROR_LEVEL, ER_CONSENSUS_READ_METADATA_ERROR, "consensus_info");
  abort();
  return 1;
}

void Consensus_info::end_info() {
  DBUG_TRACE;

  if (!inited) return;

  mysql_mutex_destroy(&LOCK_consensus_info);
  handler->end_info();
  inited = 0;
  return;
}

int Consensus_info::flush_info(bool force, bool need_commit) {
  int error = 0;
  DBUG_TRACE;

  if (!inited) return 0;

  mysql_mutex_lock(&LOCK_consensus_info);

  if (write_info(handler))
    goto err;

  error = handler->flush_info(force);

  if (error) {
    if (force && need_commit && current_thd)
      ha_rollback_trans(current_thd, true);
    goto err;
  } else if (force && current_thd && need_commit) {
    error = ha_commit_trans(current_thd, true, true);
  }

  mysql_mutex_unlock(&LOCK_consensus_info);
  return error;
err:
  mysql_mutex_unlock(&LOCK_consensus_info);
  LogErr(ERROR_LEVEL, ER_CONSENSUS_WRITE_METADATA_ERROR, "consensus_info");
  abort();
  return 1;
}

bool Consensus_info::set_info_search_keys(Rpl_info_handler *to) {
  DBUG_TRACE;
  if (to->set_info(0, (int)get_number_info_consensus_fields()))
    return true;
  return false;
}

bool Consensus_info::read_info(Rpl_info_handler *from) {
  ulong temp_vote_for = 0;
  ulong temp_current_term = 0;
  ulong temp_recover_status = 0;
  ulong temp_local_term = 0;
  ulong temp_start_apply_index = 0;
  char temp_cluster_id[256];
  char temp_cluster_info[CLUSTER_CONF_STR_LENGTH];
  char temp_cluster_learner_info[CLUSTER_CONF_STR_LENGTH];
  ulong temp_cluster_recover_index = 0;
  DBUG_TRACE;

  if (from->prepare_info_for_read() ||
      !!from->get_info(consensus_config_name, sizeof(consensus_config_name),
                       ""))
    return true;

  if (!!from->get_info(&temp_vote_for, 0) ||
      !!from->get_info(&temp_current_term, 0) ||
      !!from->get_info(&temp_recover_status, 0) ||
      !!from->get_info(&temp_local_term, 0) ||
      !!from->get_info(&temp_start_apply_index, 0) ||
      !!from->get_info(temp_cluster_id, sizeof(temp_cluster_info), "") ||
      !!from->get_info(temp_cluster_info, sizeof(temp_cluster_info), "") ||
      !!from->get_info(temp_cluster_learner_info,
                       sizeof(temp_cluster_learner_info), "") ||
      !!from->get_info(&temp_cluster_recover_index, 0))
    return true;

  vote_for = temp_vote_for;
  current_term = temp_current_term;
  recover_status = temp_recover_status;
  last_leader_term = temp_local_term;
  start_apply_index = temp_start_apply_index;
  cluster_id = std::string(temp_cluster_id, strlen(temp_cluster_id));
  cluster_info = std::string(temp_cluster_info, strlen(temp_cluster_info));
  cluster_learner_info =
      std::string(temp_cluster_learner_info, strlen(temp_cluster_learner_info));
  cluster_recover_index = temp_cluster_recover_index;

  return false;
}

bool Consensus_info::write_info(Rpl_info_handler *to) {
  DBUG_TRACE;
  if (to->prepare_info_for_write() ||
      to->set_info((int)get_number_info_consensus_fields()) ||
      to->set_info((ulong)vote_for) || to->set_info((ulong)current_term) ||
      to->set_info((ulong)recover_status) ||
      to->set_info((ulong)last_leader_term) ||
      to->set_info((ulong)start_apply_index) ||
      to->set_info(cluster_id.c_str()) || to->set_info(cluster_info.c_str()) ||
      to->set_info(cluster_learner_info.c_str()) ||
      to->set_info((ulong)cluster_recover_index))
    return true;
  return false;
}

size_t Consensus_info::get_number_info_consensus_fields() {
  return sizeof(info_consensus_fields) / sizeof(info_consensus_fields[0]);
}

void Consensus_info::set_nullable_fields(MY_BITMAP *nullable_fields) {
  bitmap_init(nullable_fields, nullptr,
              Consensus_info::get_number_info_consensus_fields());
  bitmap_clear_all(nullable_fields);
}
