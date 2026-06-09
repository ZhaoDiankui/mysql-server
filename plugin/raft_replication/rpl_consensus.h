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

#ifndef RPL_CONSENSUS_INCLUDED
#define RPL_CONSENSUS_INCLUDED

#include <string>
#include <vector>
#include <atomic>

#include "my_inttypes.h"

class ConsensusLogManager;
class ConsensusMeta;
class ConsensusStateProcess;

typedef enum State {
  FOLLOWER,
  CANDIDATE,
  LEADER,
  LEARNER,
  NOROLE,
} StateType;

typedef struct cluster_global_info {
  uint64 server_id;
  std::string ip_port;
  uint64 match_index;
  uint64 next_index;
  std::string role;
  bool has_voted;
  bool force_sync;
  uint election_weight;
  uint64 applied_index;
  uint64 learner_source;
  bool pipelining;
  bool use_applied;
} cluster_global_info;

typedef struct cluster_local_info {
  uint64 server_id;
  uint64 current_term;
  uint64 commit_index;
  uint64 last_apply_index;
  uint64 last_log_term;
  uint64 last_log_index;
  uint64 voted_for;
  std::string role;
  std::string current_leader_ip_port;
} cluster_local_info;

typedef struct cluster_helthy_info {
  uint64 server_id;
  std::string addr;
  std::string role;
  bool connected;
  uint64 log_delay;
  uint64 apply_delay;
} cluster_helthy_info;

typedef struct cluster_stats {
  uint64 count_msg_append_log;
  uint64 count_msg_request_vote;
  uint64 count_heartbeat;
  uint64 count_on_msg_append_log;
  uint64 count_on_msg_request_vote;
  uint64 count_on_heartbeat;
  uint64 count_replicate_log;
  uint64 count_log_meta_get_in_cache;
  uint64 count_log_meta_get_total;
} cluster_stats;

typedef struct cluster_memebership_change {
  std::string time;
  std::string command;
} cluster_memebership_change;

extern std::atomic<bool> rpl_consensus_inited;
extern std::atomic<bool> rpl_consensus_ready;
extern int rpl_consensus_init(bool is_learner, uint64 mock_start_index,
                              ConsensusLogManager *consensus_log_manager,
                              ConsensusMeta *consensus_meta,
                              ConsensusStateProcess *consensus_state_process);

extern bool rpl_consensus_is_ready();
extern void rpl_consensus_set_ready();
extern bool rpl_consensus_is_shutdown();
extern void rpl_consensus_shutdown();
extern void rpl_consensus_cleanup();

extern bool rpl_consensus_is_leader();

extern uint64 rpl_consensus_get_term();
extern uint64 rpl_consensus_check_commit_index(uint64 index, uint64 term);
extern uint64 rpl_consensus_get_applied_index();
extern uint64 rpl_consensus_get_commit_index();
extern void rpl_consensus_update_applied_index(uint64 log_index);
extern uint64 rpl_consensus_log_get_term();
extern uint64 rpl_consensus_get_leader_term();

extern void rpl_consensus_write_log_cache_done();
extern void rpl_consensus_write_log_done(uint64 log_index);
extern void rpl_consensus_write_log_done_internal(uint64 log_index,
                                                  bool force_send);

extern uint64 rpl_consensus_wait_commit_index_update(uint64 log_index,
                                                     uint64 term);
extern uint64 rpl_consensus_timed_wait_commit_index_update(uint64 log_index,
                                                           uint64 term,
                                                           uint64 timeout);
extern void rpl_consensus_set_last_noncommit_dep_index(uint64 log_index);

extern uint rpl_consensus_get_cluster_global_info(
    std::vector<cluster_global_info> *cgis, bool check_leader);
extern void rpl_consensus_get_cluster_local_info(cluster_local_info *cli);
extern uint rpl_consensus_get_helthy_info(
    std::vector<cluster_helthy_info> *chis);
extern void rpl_consensus_get_stats(cluster_stats *cs);
extern void rpl_consensus_get_member_changes(
    std::vector<cluster_memebership_change> *cmcs);

extern void rpl_consensus_set_checksum_mode(bool mode);
extern void rpl_consensus_set_disable_election(bool disable_election,
                                               bool disable_stepdown);
extern void rpl_consensus_set_enable_dynamic_easy_index(
    bool consensus_dynamic_easyindex);
extern void rpl_consensus_set_replicate_with_cache_log(
    bool consensus_replicate_with_cache_log);
extern void rpl_consensus_set_compact_old_mode(bool consensus_old_compact_mode);
extern void rpl_consensus_set_force_sync_epoch_diff(
    uint64 consensus_force_sync_epoch_diff);
extern void rpl_consensus_set_new_follower_threshold(
    uint64 consensus_new_follower_threshold);
extern void rpl_consensus_set_send_packet_timeout(
    uint64 consensus_send_timeout);
extern void rpl_consensus_set_send_learner_timeout(
    uint64 consensus_learner_timeout);
extern void rpl_consensus_set_enable_learner_pipelining(
    bool consensus_learner_pipelining);
extern void rpl_consensus_set_configure_change_timeout(
    uint64 consensus_configure_change_timeout);
extern void rpl_consensus_set_max_packet_size(uint64 consensus_max_packet_size);
extern void rpl_consensus_reset_msg_compress_option();
extern int rpl_consensus_set_msg_compress_option(int type, size_t threshold,
                                                 bool checksum,
                                                 const std::string &address);
extern void rpl_consensus_set_pipelining_timeout(
    uint64 consensus_pipelining_timeout);
extern void rpl_consensus_set_large_batch_ratio(
    uint64 consensus_large_batch_ratio);
extern void rpl_consensus_set_max_delay_index(uint64 consensus_max_delay_index);
extern void rpl_consensus_set_min_delay_index(uint64 consensus_min_delay_index);
extern void rpl_consensus_set_optimistic_heartbeat(
    bool consensus_optimistic_heartbeat);
extern void rpl_consensus_set_sync_follower_meta_interval(
    uint64 consensus_sync_follower_meta_interval);
extern void rpl_consensus_reset_flow_control();
extern uint64 rpl_consensus_get_server_id(const std::string &addr);
extern void rpl_consensus_set_flow_control(uint64 serverId, int64_t fc);
extern void rpl_consensus_set_alert_log_level(uint64 consensus_log_level);
extern void rpl_consensus_force_promote();
extern void rpl_consensus_set_enable_auto_reset_match_index(
    bool consensus_auto_reset_match_index);
extern void rpl_consensus_set_enable_learner_heartbeat(
    bool consensus_learner_heartbeat);
extern void rpl_consensus_set_enable_auto_leader_transfer(
    bool consensus_auto_leader_transfer);
extern void rpl_consensus_set_auto_leader_transfer_check_seconds(
    uint64 consensus_auto_leader_transfer_check_seconds);

extern uint64 rpl_consensus_get_easy_alloc_byte();

extern int rpl_consensus_get_cluster_size();
extern int rpl_consensus_set_cluster_id(const std::string &cluster_id);
extern int rpl_consensus_transfer_leader(const std::string &ip_port);
extern int rpl_consensus_transfer_leader_by_id(uint64 target_id);
extern int rpl_consensus_add_follower(std::string &addr);
extern int rpl_consensus_downgrade_follower(const std::string &addr);
extern int rpl_consensus_add_learners(std::vector<std::string> &info_vector);
extern int rpl_consensus_drop_learners(std::vector<std::string> &info_vector);
extern int rpl_consensus_upgrade_learner(std::string &addr);
extern int rpl_consensus_sync_all_learners(
    std::vector<std::string> &info_vector);
extern int rpl_consensus_configure_member(const std::string &addr,
                                          bool force_sync,
                                          uint election_weight);
extern void rpl_consensus_force_fix_match_index(const std::string &addr,
                                                uint64 match_index);
extern int rpl_consensus_force_single_leader();
extern int rpl_consensus_configure_learner(const std::string &addr,
                                           const std::string &source,
                                           bool use_applied);
extern int rpl_consensus_force_purge_log(bool local, uint64 index);

extern const char *rpl_consensus_protocol_default_error();
extern const char *rpl_consensus_protocol_error(int error_code);
#endif
