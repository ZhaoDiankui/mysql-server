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

#ifndef CONSENSUS_LOG_MANAGER_INCLUDE
#define CONSENSUS_LOG_MANAGER_INCLUDE
#include <atomic>
#include <vector>

#include "consensus_fifo_cache_manager.h"
#include "consensus_log_index.h"
#include "consensus_prefetch_manager.h"

#include "my_macros.h"

#include "sql/binlog.h"
#include "sql/binlog_ostream.h"

enum Consensus_log_event_flag {
  FLAG_GU1 = 1,
  FLAG_GU2 = 1 << 1, /* FLAG_GU2 = 3 - FLAG_GU1 */
  FLAG_LARGE_TRX = 1 << 2,
  FLAG_LARGE_TRX_END = 1 << 3,
  FLAG_CONFIG_CHANGE = 1 << 4,
  FLAG_BLOB = 1 << 5,
  FLAG_BLOB_END = 1 << 6,
  FLAG_BLOB_START = 1 << 7, /* we should mark the start for SDK */
  FLAG_ROTATE = 1 << 8
};

struct ConsensusLogEntry {
  uint64 term;
  uint64 index;
  size_t buf_size;
  uchar *buffer;
  bool outer; /* whether created by consensus module */
  uint flag;  /* atomic flag marked */
  uint64 checksum;
};

class ConsensusLogManager {
 public:
  ConsensusLogManager()
      : inited(false),
        next_event_rotate_flag(false),
        start_without_log(false),
        prefetch_manager(nullptr),
        fifo_cache_manager(nullptr),
        log_file_index(nullptr),
        current_index(0),
        cache_index(0),
        sync_index(0),
        last_log_term(0),
        local_system_log_index(0),
        has_pending_local_system_log(false),
        commit_index(0),
        in_large_trx(false),
        consensus_commit_advance_is_running(false),
        event_tv_sec(0) {}
  ~ConsensusLogManager() {}

  int init(uint64 max_fifo_cache_size_arg, uint64 max_prefetch_cache_size_arg,
           uint64 fake_current_index = 0);
  int cleanup();

  IO_CACHE_binlog_cache_storage *get_log_cache() { return cache_log; }
  std::string get_empty_log() { return empty_log_event_content; }

  bool get_next_event_rotate_flag() { return next_event_rotate_flag; }
  void set_next_event_rotate_flag(bool arg) { next_event_rotate_flag = arg; }

  ConsensusLogIndex *get_log_file_index() { return log_file_index; }
  ConsensusPreFetchManager *get_prefetch_manager() { return prefetch_manager; }
  ConsensusFifoCacheManager *get_fifo_cache_manager() {
    return fifo_cache_manager;
  }

  // consensus index
  inline mysql_rwlock_t *get_consensuslog_truncate_lock() {
    return &LOCK_consensuslog_truncate;
  }
  uint64 get_current_index() { return current_index; }
  uint64 get_cache_index() { return cache_index; }
  uint64 get_sync_index() { return sync_index; }
  uint64 get_final_sync_index();
  void set_current_index(uint64 current_index_arg) {
    current_index = current_index_arg;
  }
  void incr_current_index() { current_index++; }
  void set_cache_index(uint64 arg) { cache_index = arg; }
  void set_sync_index(uint64 arg) { sync_index = arg; }
  void set_sync_index_if_greater(uint64 sync_index_arg);

  bool get_start_without_log() { return start_without_log; }
  void set_start_without_log(bool start_without_log_arg) {
    start_without_log = start_without_log_arg;
  }

  uint64 get_last_log_term() { return last_log_term; }
  void set_last_log_term(uint64 arg) { last_log_term = arg; }

  // consensus commit index
  uint64 get_commit_index() { return commit_index; }
  bool advance_commit_index_if_greater(uint64 arg, bool force);
  void set_in_large_trx(bool arg) { in_large_trx = arg; }
  int try_advance_commit_position(uint64 timeout);

  mysql_rwlock_t *get_consensuslog_rotate_lock() {
    return &LOCK_consensuslog_rotate;
  }
  bool get_enable_rotate() {
    return !has_pending_local_system_log && !in_large_trx;
  }

  // for log operation
  int write_log_entry(ConsensusLogEntry &log, uint64 *consensus_index,
                      bool with_check = false);
  int write_log_entries(std::vector<ConsensusLogEntry> &logs,
                        uint64 *max_index);
  int get_log_entry(uint64 channel_id, uint64 consensus_index,
                    uint64 *consensus_term, std::string &log_content,
                    bool *outer, uint *flag, uint64 *checksum, bool fast_fail);
  int get_log_directly(uint64 consensus_index, uint64 *consensus_term,
                       std::string &log_content, bool *outer, uint *flag,
                       uint64 *checksum, bool need_content = true);
  uint64_t get_left_log_size(uint64 start_log_index, uint64 max_packet_size);
  int prefetch_log_directly(THD *thd, uint64 channel_id,
                            uint64 consensus_index);
  int get_log_position(uint64 consensus_index, bool need_lock, char *log_name,
                       my_off_t *pos);
  int get_log_end_position(uint64 consensus_index, bool need_lock,
                           char *log_name, my_off_t *pos);
  uint64 get_next_index_from_position(const char *log_file_name,
                                      my_off_t log_pos, bool need_lock = true);
  uint64 get_next_trx_index(uint64 consensus_index, bool need_lock = true);
  int truncate_log(uint64 consensus_index);
  int purge_log(uint64 consensus_index);
  uint64 get_exist_log_length();

  void set_event_timestamp(uint32 t) { event_tv_sec.store(t); }
  uint32 get_event_timestamp() { return event_tv_sec.load(); }

  int start_consensus_commit_advance_thread();
  int stop_consensus_commit_advance_thread();

 private:
  bool set_local_system_log_if_greater(uint64 local_sytem_log_index_arg,
                                       bool force_signal);
  void truncate_local_system_log_if_lesser(uint64 truncate_index_arg);
  bool wait_for_uncommitted_local_system_log(uint64 timetout);

 private:
  bool inited;
  bool next_event_rotate_flag;
  bool start_without_log;

  std::string empty_log_event_content;
  IO_CACHE_binlog_cache_storage *cache_log;  // cache a ConsensusLogEntry, and
                                             // communicate with algorithm layer
  ConsensusPreFetchManager *prefetch_manager;     // prefetch module
  ConsensusFifoCacheManager *fifo_cache_manager;  // fifo cache module
  ConsensusLogIndex *log_file_index;              // consensus log file index

  mysql_rwlock_t LOCK_consensuslog_truncate;
  std::atomic<uint64> current_index;  // current log index in the log system
  std::atomic<uint64> cache_index;    // last cache log entry
  std::atomic<uint64> sync_index;     // last log entry has been synced

  std::atomic<uint64> last_log_term;   // last log term, don't decrease if truncated

  mysql_rwlock_t LOCK_consensuslog_rotate;
  uint64 local_system_log_index;  // last local system log
                                  // index write by raft protocal
  std::atomic<bool>
      has_pending_local_system_log;  // do not rotate if local system log is
                                     // uncommitted, such as configchange.
  std::atomic<uint64> commit_index;  // last commit index advanced
  std::atomic<bool> in_large_trx;    // do not rotate if in middle of large trx
  std::atomic<bool> consensus_commit_advance_is_running;
  my_thread_handle consensus_commit_advance_thread_handler;
  mysql_cond_t COND_consensuslog_commit_advance;
  mysql_mutex_t LOCK_consensuslog_commit_advance;

  std::atomic<uint32>
      event_tv_sec;  // last log event timestamp received from leader
};

extern ConsensusLogManager consensus_log_manager;

uint64 show_fifo_cache_size(THD *, SHOW_VAR *var, char *buff);
uint64 show_log_count_in_fifo_cache(THD *, SHOW_VAR *var, char *buff);
uint64 show_first_index_in_fifo_cache(THD *, SHOW_VAR *var, char *buff);

#endif  // CONSENSUS_LOG_MANAGER_INCLUDE
