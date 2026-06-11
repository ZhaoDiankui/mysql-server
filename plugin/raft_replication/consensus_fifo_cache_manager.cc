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

#include "consensus_fifo_cache_manager.h"
#include "consensus_log_manager.h"
#include "plugin_psi.h"
#include "system_variables.h"

#include "mysql/thread_pool_priv.h"
#include "mysqld_error.h"

#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/log.h"

#include "sql/mysqld.h"

int ConsensusFifoCacheManager::init(uint64 max_log_cache_size_arg) {
  DBUG_TRACE;

  max_log_cache_size = max_log_cache_size_arg;
  fifo_cache_size = 0;
  lock_blob_index = 0;
  current_log_count = 0;

  key_memory_cache_mem_root = key_memory_ConsensusLogManager;
  key_LOCK_consensuslog_cache = key_rwlock_ConsensusLog_log_cache_lock;

  mysql_rwlock_init(key_LOCK_consensuslog_cache, &LOCK_consensuslog_cache);

  log_cache_list = new ConsensusLogEntry[reserve_list_size];
  rleft = rright = 0;

  mysql_mutex_init(key_fifo_cache_cleaner, &cleaner_mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(0, &cleaner_cond);

  is_running = true;
  if (mysql_thread_create(key_thread_cleaner, &cleaner_handle, nullptr,
                          fifo_cleaner_wrapper, (void *)this)) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_CREATE_THRERD_ERROR, "fifo_cleaner");
    abort();
  }

  inited = true;
  return 0;
}

int ConsensusFifoCacheManager::cleanup() {
  DBUG_TRACE;

  if (inited) {
    is_running = false;
    mysql_cond_signal(&cleaner_cond);
    my_thread_join(&cleaner_handle, nullptr);
    mysql_rwlock_destroy(&LOCK_consensuslog_cache);
    for (size_t i = rleft; i < rright; i = (i + 1) % reserve_list_size) {
      if (log_cache_list[i].buf_size > 0) my_free(log_cache_list[i].buffer);
    }
    delete[] log_cache_list;

    mysql_mutex_destroy(&cleaner_mutex);
    mysql_cond_destroy(&cleaner_cond);
  }
  return 0;
}

int ConsensusFifoCacheManager::get_log_from_cache(uint64 index, uint64 *term,
                                                  std::string &log_content,
                                                  bool *outer, uint *flag,
                                                  uint64 *checksum) {
  DBUG_TRACE;
  DBUG_EXECUTE_IF("skip_consensus_fifo_cache", { return ALREADY_SWAP_OUT; });

  mysql_rwlock_rdlock(&LOCK_consensuslog_cache);
  if (max_log_cache_size == 0 || current_log_count == 0 ||
      index < log_cache_list[rleft].index) {
    mysql_rwlock_unlock(&LOCK_consensuslog_cache);
    return ALREADY_SWAP_OUT;
  }

  size_t lasti = (rright + reserve_list_size - 1) % reserve_list_size;
  if (index > log_cache_list[lasti].index /* out of range */) {
    LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_FIFO_CACHE_OUT_OF_RANGE,
                 log_cache_list[lasti].index, index);
    mysql_rwlock_unlock(&LOCK_consensuslog_cache);
    return OUT_OF_RANGE;
  }

  ConsensusLogEntry log_entry =
      log_cache_list[(rleft + index - log_cache_list[rleft].index) %
                     reserve_list_size];
  *term = log_entry.term;
  *outer = log_entry.outer;
  *flag = log_entry.flag;
  log_content.assign((char *)(log_entry.buffer), log_entry.buf_size);
  *checksum = log_entry.checksum;

  mysql_rwlock_unlock(&LOCK_consensuslog_cache);
  return 0;
}

int ConsensusFifoCacheManager::add_log_to_cache(uint64 term, uint64 index,
                                                size_t buf_size, uchar *buffer,
                                                bool outer, uint flag,
                                                uint64 checksum,
                                                bool reuse_buffer) {
  DBUG_TRACE;

  if (opt_consensus_disable_fifo_cache) return 1;

  uchar *new_buffer = 0;

  /* spin loop to make sure enough space left */
  while (current_log_count >= reserve_list_size - 1)
    mysql_cond_signal(&cleaner_cond);

  if (reuse_buffer)
    new_buffer = buffer;
  else
    new_buffer = (uchar *)my_memdup(key_memory_cache_mem_root, (char *)buffer,
                                    buf_size, MYF(MY_WME));

  log_cache_list[rright] = (ConsensusLogEntry){
      term, index, buf_size, new_buffer, outer, flag, checksum};
  rright = (rright + 1) % reserve_list_size;
  fifo_cache_size += buf_size;
  current_log_count++;

  mysql_cond_signal(&cleaner_cond);

  consensus_log_manager.set_cache_index(index);

  return 0;
}

int ConsensusFifoCacheManager::trunc_log_from_cache(uint64 index) {
  DBUG_TRACE;

  consensus_log_manager.set_cache_index(index - 1);

  mysql_rwlock_wrlock(&LOCK_consensuslog_cache);

  LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_FIFO_CACHE_TRUNCATE, "before",
               log_cache_list[rleft].index,
               (rright + reserve_list_size - rleft) % reserve_list_size);

  if (max_log_cache_size == 0 || current_log_count == 0) {
    mysql_rwlock_unlock(&LOCK_consensuslog_cache);
    return 0;
  }

  size_t start_point = 0;
  if (index <= log_cache_list[rleft].index) {
    start_point = rleft;
  } else {
    start_point =
        (rleft + index - log_cache_list[rleft].index) % reserve_list_size;
  }

  size_t cur_pos = start_point;
  // truncate [start_point, rright)
  while (cur_pos != rright) {
    if (log_cache_list[cur_pos].buf_size > 0)
      my_free(log_cache_list[cur_pos].buffer);
    fifo_cache_size -= log_cache_list[cur_pos].buf_size;
    current_log_count--;
    cur_pos = (cur_pos + 1) % reserve_list_size;
  }
  rright = start_point;

  LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_FIFO_CACHE_TRUNCATE, "after",
               log_cache_list[rleft].index,
               (rright + reserve_list_size - rleft) % reserve_list_size);

  mysql_rwlock_unlock(&LOCK_consensuslog_cache);
  return 0;
}

uint64 ConsensusFifoCacheManager::get_log_size_from_cache(
    uint64 begin_index, uint64 end_index, uint64 max_packet_size) {
  uint64 total_size = 0;
  DBUG_TRACE;

  mysql_rwlock_rdlock(&LOCK_consensuslog_cache);

  size_t lasti = (rright + reserve_list_size - 1) % reserve_list_size;
  size_t lefti = log_cache_list[rleft].index;
  for (uint64 i = begin_index; i <= end_index; i++) {
    if (i < lefti || i > log_cache_list[lasti].index)
      break;
    else
      total_size +=
          log_cache_list[(rleft + i - lefti) % reserve_list_size].buf_size;

    if (total_size > max_packet_size) break;
  }

  mysql_rwlock_unlock(&LOCK_consensuslog_cache);
  return total_size;
}

uint64 ConsensusFifoCacheManager::get_first_index_of_fifo_cache() {
  uint64 ret = 0;
  DBUG_TRACE;

  mysql_rwlock_rdlock(&LOCK_consensuslog_cache);
  ret = log_cache_list[rleft].index;
  mysql_rwlock_unlock(&LOCK_consensuslog_cache);

  return ret;
}

void ConsensusFifoCacheManager::set_lock_blob_index(
    uint64 lock_blob_index_arg) {
  LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_FIFO_CACHE_SET_BLOB_INDEX,
               lock_blob_index_arg);
  lock_blob_index = lock_blob_index_arg;
}

void ConsensusFifoCacheManager::clean_consensus_fifo_cache() {
  DBUG_TRACE;

  mysql_mutex_lock(&cleaner_mutex);

  while (is_running.load()) {
    mysql_cond_wait(&cleaner_cond, &cleaner_mutex);

    mysql_rwlock_wrlock(&LOCK_consensuslog_cache);

    while ((fifo_cache_size > max_log_cache_size ||
            (current_log_count + 1) >= reserve_list_size) &&
           (current_log_count > 1)) {
      if (log_cache_list[rleft].index == lock_blob_index &&
          (current_log_count + 1) < reserve_list_size)
        break;

      ConsensusLogEntry old_log = log_cache_list[rleft];
      if (old_log.buf_size > 0) my_free(old_log.buffer);
      fifo_cache_size -= old_log.buf_size;
      current_log_count--;

      rleft = (rleft + 1) % reserve_list_size;
    }

    mysql_rwlock_unlock(&LOCK_consensuslog_cache);
  }

  mysql_mutex_unlock(&cleaner_mutex);
}

void *fifo_cleaner_wrapper(void *arg) {
  ConsensusFifoCacheManager *fifo = (ConsensusFifoCacheManager *)arg;
  DBUG_TRACE;
  fifo->clean_consensus_fifo_cache();
  return nullptr;
}
