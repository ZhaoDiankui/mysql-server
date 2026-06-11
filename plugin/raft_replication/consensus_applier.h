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

#ifndef CONSENSUS_APPLIER_INCLUDE
#define CONSENSUS_APPLIER_INCLUDE
#include <atomic>

#include "my_inttypes.h"
#include "my_io.h"
#include "my_macros.h"
#include "my_sys.h"

class Master_info;
class Log_event;
class Relay_log_info;

class ConsensusApplier {
 public:
  ConsensusApplier()
      : inited(false),
        apply_index(0),
        real_apply_index(0),
        apply_term(0),
        in_large_trx(false),
        apply_catchup(false),
        stop_term(UINT64_MAX) {
  }
  ~ConsensusApplier() {}

  int init();
  int cleanup();

  uint64 get_apply_index() { return apply_index; }
  uint64 get_real_apply_index() { return real_apply_index; }
  uint64 get_apply_term() { return apply_term; }
  uint64 get_stop_term() { return stop_term; }
  bool get_in_large_trx() { return in_large_trx; }
  void set_apply_index(uint64 apply_index_arg) {
    apply_index = apply_index_arg;
  }
  void set_real_apply_index(uint64 real_apply_index_arg) {
    real_apply_index = real_apply_index_arg;
  }
  void set_apply_term(uint64 apply_term_arg) { apply_term = apply_term_arg; }
  void set_stop_term(uint64 stop_term_arg) { stop_term = stop_term_arg; }
  void set_apply_catchup(bool apply_catchup_arg) {
    apply_catchup = apply_catchup_arg;
  }
  void set_in_large_trx(bool in_large_trx_arg) {
    in_large_trx = in_large_trx_arg;
  }

  mysql_mutex_t *get_apply_thread_lock() {
    return &LOCK_consensus_applier_catchup;
  }
  mysql_cond_t *get_catchup_cond() { return &COND_consensus_applier_catchup; }

  void wait_replay_log_finished();
  void wait_apply_threads_stop();

 private:
  bool inited;
  std::atomic<uint64> apply_index;       // sql thread coordinator apply index
  std::atomic<uint64> real_apply_index;  // for large trx
  std::atomic<uint64> apply_term;        // sql thread coordinator apply term
  std::atomic<bool> in_large_trx;

  /* Consensus applier exit */
  mysql_mutex_t LOCK_consensus_applier_catchup;
  mysql_cond_t COND_consensus_applier_catchup;  // whether apply
                                                // thread arrived commit index
  bool apply_catchup;  // protect by LOCK_consensus_applier_catchup, determine
                       // whether apply thread catchup
  std::atomic<uint64> stop_term;  // atomic read/write, mark sql thread
                                  // coordinator stop condition
};

int init_consensus_replica();
int start_consensus_replica();
void end_consensus_replica();
bool is_consensus_applier_running();
bool applier_mts_recovery_groups(Relay_log_info *rli);
int mts_recovery_max_consensus_index();
int start_consensus_apply_threads(Master_info *mi);
int check_exec_consensus_log_end_condition(Relay_log_info *rli);
int update_consensus_apply_pos(Relay_log_info *rli, Log_event *ev);
int calculate_consensus_apply_start_pos(Relay_log_info *rli);
void update_consensus_applied_index(uint64 applied_index);

extern ConsensusApplier consensus_applier;

#endif
