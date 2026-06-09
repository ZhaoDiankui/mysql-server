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

#ifndef CONSENSUS_FIFO_CACHE_MANAGER_INCLUDE
#define CONSENSUS_FIFO_CACHE_MANAGER_INCLUDE

#include <atomic>
#include <thread>
#include "sql/mysqld.h"

struct ConsensusLogEntry;

enum ConsensusLogCacheResultCode {
  SUCCESS = 0,
  ALREADY_SWAP_OUT = 1,
  OUT_OF_RANGE = 2,
  FULL = 3,
  INTERRUPT = 4,
  EMPTY = 5
};

#define RESERVE_LIST_SIZE (1024 * 1024)

class ConsensusFifoCacheManager {
 public:
  ConsensusFifoCacheManager()
      : inited(false), reserve_list_size(RESERVE_LIST_SIZE) {}
  ~ConsensusFifoCacheManager() {}

  int init(uint64 max_log_cache_size_arg);
  int cleanup();
  int add_log_to_cache(uint64 term, uint64 index, size_t buf_size,
                       uchar *buffer, bool outer, uint flag,
                       uint64 checksum = 0, bool reuse_buffer = false);
  int get_log_from_cache(uint64 index, uint64 *term, std::string &log_content,
                         bool *outer, uint *flag, uint64 *checksum);
  int trunc_log_from_cache(uint64 index);
  uint64 get_log_size_from_cache(uint64 begin_index, uint64 end_index,
                                 uint64 max_packet_size);
  uint64 get_fifo_cache_size() { return fifo_cache_size; }
  uint64 get_first_index_of_fifo_cache();
  uint64 get_fifo_cache_log_count() { return current_log_count; }
  void set_max_log_cache_size(uint64 max_log_cache_size_arg) {
    max_log_cache_size = max_log_cache_size_arg;
  }
  void set_lock_blob_index(uint64 lock_blob_index_arg);

  void clean_consensus_fifo_cache();

 private:
  bool inited;
  PSI_memory_key key_memory_cache_mem_root;
  PSI_rwlock_key key_LOCK_consensuslog_cache;
  mysql_rwlock_t LOCK_consensuslog_cache;  // used to protect log cache
  std::atomic<size_t> rleft, rright;
  ConsensusLogEntry
      *log_cache_list;  // ring buffer: data range is [rleft, rright)
  std::atomic<uint64> max_log_cache_size;  // FIFO CACHE MAX SIZE
  std::atomic<uint64> fifo_cache_size;     // FIFO cache status
  uint64 lock_blob_index;
  std::atomic<uint64> current_log_count;
  std::atomic<bool> is_running;
  mysql_mutex_t cleaner_mutex;
  mysql_cond_t cleaner_cond;
  my_thread_handle cleaner_handle;

  const uint64_t reserve_list_size;  // reserve size for ring buffer
};

void *fifo_cleaner_wrapper(void *arg);

#endif  // CONSENSUS_FIFO_CACHE_MANAGER_INCLUDE
