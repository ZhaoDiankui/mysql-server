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

#ifndef CONSENSUS_INFO_INCLUDE
#define CONSENSUS_INFO_INCLUDE

#include <atomic>
#include <string>
#include "mysql/components/services/bits/psi_rwlock_bits.h"
#include "sql/rpl_info.h"

class Consensus_info : public Rpl_info {
  friend class Rpl_info_factory;

 public:
  Consensus_info(
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
  );

  uint64 get_recover_status() { return recover_status; }
  uint64 get_vote_for() { return vote_for; }
  uint64 get_current_term() { return current_term; }
  uint64 get_last_leader_term() { return last_leader_term; }
  uint64 get_start_apply_index() { return start_apply_index; }
  std::string &get_cluster_id() { return cluster_id; }
  std::string &get_cluster_info() { return cluster_info; }
  std::string &get_cluster_learner_info() { return cluster_learner_info; }
  uint64 get_cluster_recover_index() { return cluster_recover_index; }

  void set_recover_status(uint64 recover_status_arg) {
    recover_status = recover_status_arg;
  }
  void set_vote_for(uint64 vote_for_arg) { vote_for = vote_for_arg; }
  void set_current_term(uint64 current_term_arg) {
    current_term = current_term_arg;
  }
  void set_last_leader_term(uint64 last_leader_term_arg) {
    last_leader_term = last_leader_term_arg;
  }
  void set_start_apply_index(uint64 start_apply_index_arg) {
    start_apply_index = start_apply_index_arg;
  }
  void set_cluster_id(const std::string &cluster_id_arg) {
    cluster_id = cluster_id_arg;
  }
  void set_cluster_info(const std::string &cluster_info_arg) {
    cluster_info = cluster_info_arg;
  }
  void set_cluster_learner_info(const std::string &cluster_learner_info_arg) {
    cluster_learner_info = cluster_learner_info_arg;
  }
  void set_cluster_recover_index(uint64 cluster_recover_index_arg) {
    cluster_recover_index = cluster_recover_index_arg;
  }

 public:
  int init_info();
  void end_info();
  int flush_info(bool force, bool need_commit = false);
  bool set_info_search_keys(Rpl_info_handler *to) override;
  static size_t get_number_info_consensus_fields();

  virtual const char *get_for_channel_str(
      bool upper_case [[maybe_unused]]) const override {
    return nullptr;
  }

  static void set_nullable_fields(MY_BITMAP *nullable_fields);

 private:
  bool read_info(Rpl_info_handler *from) override;
  bool write_info(Rpl_info_handler *to) override;

 private:
  static const int CLUSTER_CONF_STR_LENGTH = 5000;
  mysql_mutex_t LOCK_consensus_info;  // protect flush
  uint64 vote_for;                    // used by consensus algorithm layer
  uint64 current_term;                // used by consensus algorithm layer
  uint64 recover_status;  // used by replay thread to determine startpoint;
                          // 0:followrer  1:leader
  std::atomic<uint64> last_leader_term;  // used by determine apply point
  std::atomic<uint64>
      start_apply_index;  // used by determine apply point, relay working set 0
                          // first leader downgrade set right value
  std::string cluster_id;            // used to identify cluster
  std::string cluster_info;          // used to store consensus nodes info
  std::string cluster_learner_info;  // used to store learner nodes info
  uint64 cluster_recover_index;      // used to recover cluster config
  char consensus_config_name[FN_REFLEN];
};
#endif
