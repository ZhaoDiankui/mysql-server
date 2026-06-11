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

#ifndef CONSENSUS_APPLIER_INFO_INCLUDE
#define CONSENSUS_APPLIER_INFO_INCLUDE

#include <atomic>
#include <string>

#include "consensus_applier_worker.h"

#include "prealloced_array.h"
#include "mysql/components/services/bits/psi_rwlock_bits.h"

#include "sql/rpl_info.h"

class Relay_log_info;
class Consensus_applier_worker;

typedef Prealloced_array<Consensus_applier_worker *, 4> Consensus_worker_array;

class Consensus_applier_info : public Rpl_info {
  friend class Rpl_info_factory;

 public:
  Consensus_applier_info(
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

  int init_info();
  void end_info();
  int flush_info(bool force, bool need_commit = false);
  bool set_info_search_keys(Rpl_info_handler *to) override;
  static size_t get_number_fields();

  virtual const char *get_for_channel_str(
      bool upper_case [[maybe_unused]]) const override {
    return nullptr;
  }

  static void set_nullable_fields(MY_BITMAP *nullable_fields);

  inline void set_consensus_apply_index(uint64 log_index) {
    consensus_apply_index = log_index;
  }
  inline void set_mts_consensus_hwm_index(uint64 log_index) {
    mts_consensus_hwm_index = log_index;
  }
  inline uint64 get_consensus_apply_index() { return consensus_apply_index; }
  inline uint64 get_mts_consensus_hwm_index() {
    return mts_consensus_hwm_index;
  }

  Consensus_applier_worker *get_worker(size_t n) {
    if (workers_array_initialized) {
      if (n >= workers.size()) return nullptr;
      return workers[n];
    } else {
      return nullptr;
    }
  }

  uint64 saved_consensus_apply_index;

  bool workers_array_initialized;
  Consensus_worker_array workers;
  ulong parallel_workers;
  ulong recovery_parallel_workers;

  int commit_positions(uint64 event_consensus_index, bool to_flush);
  int rollback_positions();

  bool mts_finalize_recovery();

 private:
  bool read_info(Rpl_info_handler *from) override;
  bool write_info(Rpl_info_handler *to) override;

  mysql_mutex_t LOCK_consensus_applier_info;
  std::atomic<uint64> consensus_apply_index;
  uint64 mts_consensus_hwm_index;
};

int create_applier_workers(Relay_log_info *rli,
                           Consensus_applier_info *applier_info,
                           ulong n_workers);
void destory_applier_workers(Relay_log_info *rli,
                             Consensus_applier_info *applier_info);
#endif
