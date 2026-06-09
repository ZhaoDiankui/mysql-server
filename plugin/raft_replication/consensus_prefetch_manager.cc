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

#include "consensus_prefetch_manager.h"
#include "consensus_log_manager.h"
#include "consensus_state_process.h"
#include "plugin_psi.h"
#include "system_variables.h"

#include "mysqld_error.h"
#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/log.h"

#include "mysql/thread_pool_priv.h"
#include "sql/binlog.h"
#include "sql/rpl_rli.h"
#include "sql/sql_thd_internal_api.h"

int ConsensusPreFetchChannel::init(uint64 id, uint64 max_cache_size,
                                   uint64 prefetch_window_size,
                                   uint64 prefetch_wakeup_ratio) {
  DBUG_TRACE;
  assert (!inited);
  channel_id = id;
  max_prefetch_cache_size = max_cache_size;

  mysql_mutex_init(key_LOCK_prefetch_channel, &LOCK_prefetch_channel,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_LOCK_prefetch_request, &LOCK_prefetch_request,
                   MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_COND_prefetch_channel_cond, &COND_prefetch_channel);
  mysql_cond_init(key_COND_prefetch_request_cond, &COND_prefetch_request);

  first_index_in_cache = 0;
  is_running = true;
  stop_prefetch_flag = false;
  current_prefetch_request = 0;
  prefetch_cache_size = 0;
  from_beginning = false;
  window_size = prefetch_window_size;
  wakeup_ratio = prefetch_wakeup_ratio;
  ref_count = 0;

  if (mysql_thread_create(key_thread_prefetch, &prefetch_thread_handle, nullptr,
                          run_prefetch, (void *)this)) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_CREATE_THRERD_ERROR, "run_prefetch");
    abort();
  }

  inited = true;
  return 0;
}

int ConsensusPreFetchChannel::cleanup() {
  DBUG_TRACE;

  if (!inited) return 0;

  mysql_mutex_lock(&LOCK_prefetch_request);
  is_running = false;
  current_prefetch_request = 0;
  mysql_cond_broadcast(&COND_prefetch_channel);
  mysql_mutex_unlock(&LOCK_prefetch_request);

  mysql_mutex_lock(&LOCK_prefetch_channel);
  stop_prefetch_flag = true;
  from_beginning = true;
  mysql_cond_broadcast(&COND_prefetch_request);
  mysql_mutex_unlock(&LOCK_prefetch_channel);

  my_thread_join(&prefetch_thread_handle, nullptr);
  // wait all get request end
  while (ref_count != 0) {
    my_sleep(1000);
  }

  mysql_mutex_destroy(&LOCK_prefetch_channel);
  mysql_mutex_destroy(&LOCK_prefetch_request);
  mysql_cond_destroy(&COND_prefetch_channel);
  mysql_cond_destroy(&COND_prefetch_request);

  inited = false;
  return 0;
}

int ConsensusPreFetchChannel::add_log_to_prefetch_cache(
    uint64 term, uint64 index, size_t buf_size, uchar *buffer, bool outer,
    uint flag, uint64 checksum) {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_channel);

  if (stop_prefetch_flag || from_beginning) {
    mysql_mutex_unlock(&LOCK_prefetch_channel);
    LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_LOG_PREFETCH_STOPPED,
                 channel_id, stop_prefetch_flag, from_beginning);
    return INTERRUPT;
  }

  if ((index >= first_index_in_cache) &&
      (index < first_index_in_cache + prefetch_cache.size())) {
    // already exist in prefetch cache
    mysql_mutex_unlock(&LOCK_prefetch_channel);
    return SUCCESS;
  } else if (first_index_in_cache != 0 &&
             (index < first_index_in_cache ||
              index > first_index_in_cache + prefetch_cache.size())) {
    LogPluginErr(INFORMATION_LEVEL,
                 ER_CONSENSUS_LOG_PREFETCH_CACHE_ADD_OUT_OF_RANGE, channel_id,
                 first_index_in_cache.load(),
                 first_index_in_cache + prefetch_cache.size() - 1, index);
    for (auto iter = prefetch_cache.begin(); iter != prefetch_cache.end();
         ++iter) {
      if (iter->buf_size > 0) my_free(iter->buffer);
    }
    prefetch_cache.clear();
    first_index_in_cache = 0;
    prefetch_cache_size = 0;
  }

  while (prefetch_cache_size + buf_size > max_prefetch_cache_size &&
         prefetch_cache.size() > 0) {
    if (index > current_prefetch_request.load()) {
      if (!stop_prefetch_flag && !from_beginning)
        mysql_cond_wait(&COND_prefetch_channel, &LOCK_prefetch_channel);
      mysql_mutex_unlock(&LOCK_prefetch_channel);
      return FULL;
    } else {
      LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_LOG_PREFETCH_CACHE_SHRINK,
                   channel_id, first_index_in_cache.load(),
                   first_index_in_cache + prefetch_cache.size() - 1, index,
                   current_prefetch_request.load());
      ConsensusLogEntry old_log = prefetch_cache.front();
      if (old_log.buf_size > 0) my_free(old_log.buffer);
      prefetch_cache_size -= old_log.buf_size;
      prefetch_cache.pop_front();
      first_index_in_cache++;
    }
  }

  uchar *new_buffer = (uchar *)my_memdup(key_memory_prefetch_mem_root,
                                         (char *)buffer, buf_size, MYF(MY_WME));
  ConsensusLogEntry new_log = {term,  index, buf_size, new_buffer,
                               outer, flag,  checksum};
  if (prefetch_cache.size() == 0) {
    first_index_in_cache = index;
  }
  prefetch_cache.push_back(new_log);
  prefetch_cache_size += buf_size;

  mysql_mutex_unlock(&LOCK_prefetch_channel);
  return SUCCESS;
}

void ConsensusPreFetchChannel::add_log_to_large_trx_table(uint64 term,
                                                          uint64 index,
                                                          bool outer,
                                                          uint flag) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_prefetch_channel);
  assert(flag & (Consensus_log_event_flag::FLAG_LARGE_TRX |
                 Consensus_log_event_flag::FLAG_LARGE_TRX_END));
  ConsensusLogEntry new_log = {term, index, 0, nullptr, outer, flag, 0};
  large_trx_table.insert(std::make_pair(index, new_log));
  mysql_mutex_unlock(&LOCK_prefetch_channel);
}

void ConsensusPreFetchChannel::clear_large_trx_table() {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_prefetch_channel);
  large_trx_table.clear();
  mysql_mutex_unlock(&LOCK_prefetch_channel);
}

int ConsensusPreFetchChannel::get_log_from_prefetch_cache(
    uint64 index, uint64 *term, std::string &log_content, bool *outer,
    uint *flag, uint64 *checksum) {
  int error = SUCCESS;
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_channel);

  if (channel_id == 0) {
    /* quick path for channel 0 large trx */
    auto it = large_trx_table.find(index);
    if (it != large_trx_table.end()) {
      *term = it->second.term;
      *outer = it->second.outer;
      *flag = it->second.flag;
      log_content.assign("");
      mysql_mutex_unlock(&LOCK_prefetch_channel);
      LogPluginErr(INFORMATION_LEVEL,
                   ER_CONSENSUS_LOG_PREFETCH_CACHE_HIT_LARGE_TRX_TABLE, index);
      return SUCCESS;
    }
  }

  if (max_prefetch_cache_size == 0 ||
      first_index_in_cache == 0 /* if fifo cache is empty */) {
    error = EMPTY;
  } else if (index < first_index_in_cache) {
    LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_LOG_PREFETCH_CACHE_SWAP_OUT,
                 channel_id, first_index_in_cache.load(), index);
    for (auto iter = prefetch_cache.begin(); iter != prefetch_cache.end();
         ++iter) {
      if (iter->buf_size > 0) my_free(iter->buffer);
    }
    prefetch_cache.clear();
    first_index_in_cache = 0;
    prefetch_cache_size = 0;
    error = ALREADY_SWAP_OUT;
  } else if (index >= first_index_in_cache + prefetch_cache.size()) {
    LogPluginErr(INFORMATION_LEVEL,
                 ER_CONSENSUS_LOG_PREFETCH_CACHE_OUT_OF_RANGE, channel_id,
                 first_index_in_cache + prefetch_cache.size() - 1, index);
    while (prefetch_cache.size() > window_size) {
      ConsensusLogEntry old_log = prefetch_cache.front();
      if (old_log.buf_size > 0) my_free(old_log.buffer);
      prefetch_cache_size -= old_log.buf_size;
      prefetch_cache.pop_front();
      first_index_in_cache++;
    }
    error = OUT_OF_RANGE;
  } else {
    ConsensusLogEntry log_entry =
        prefetch_cache.at(index - first_index_in_cache);
    *term = log_entry.term;
    *outer = log_entry.outer;
    *flag = log_entry.flag;
    *checksum = log_entry.checksum;
    log_content.assign((char *)(log_entry.buffer), log_entry.buf_size);

    // truncate before , retain window_size or 1/2 max_cache_size
    // because consensus layer will always fetch index and index-1
    // at least retain two entries
    while (first_index_in_cache + window_size < index ||
           (first_index_in_cache + 1 < index &&
            prefetch_cache_size * 2 > max_prefetch_cache_size)) {
      ConsensusLogEntry old_log = prefetch_cache.front();
      if (old_log.buf_size > 0) my_free(old_log.buffer);
      prefetch_cache_size -= old_log.buf_size;
      prefetch_cache.pop_front();
      first_index_in_cache++;
    }
  }

  // wakeup prefetch thread
  if (error == ALREADY_SWAP_OUT)
    from_beginning = true;
  else
    from_beginning = false;

  if (prefetch_cache_size * wakeup_ratio <= max_prefetch_cache_size ||
      error == OUT_OF_RANGE || error == ALREADY_SWAP_OUT)
    mysql_cond_broadcast(&COND_prefetch_channel);

  mysql_mutex_unlock(&LOCK_prefetch_channel);

  return error;
}

bool ConsensusPreFetchChannel::log_exist(uint64 index) {
  bool exist = false;
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_prefetch_channel);
  if (index >= first_index_in_cache &&
      index < first_index_in_cache + prefetch_cache.size()) {
    exist = true;
  }
  mysql_mutex_unlock(&LOCK_prefetch_channel);
  return exist;
}

int ConsensusPreFetchChannel::reset_prefetch_cache() {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_channel);

  large_trx_table.clear();
  for (auto iter = prefetch_cache.begin(); iter != prefetch_cache.end();
       iter++) {
    if (iter->buf_size > 0) my_free(iter->buffer);
    prefetch_cache_size -= iter->buf_size;
  }
  prefetch_cache.clear();

  first_index_in_cache = 0;
  assert(prefetch_cache_size == 0);
  prefetch_cache_size = 0;
  from_beginning = true;
  mysql_cond_broadcast(&COND_prefetch_channel);

  mysql_mutex_unlock(&LOCK_prefetch_channel);

  return 0;
}

int ConsensusPreFetchChannel::enable_prefetch_channel() {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_prefetch_channel);
  stop_prefetch_flag = false;
  mysql_mutex_unlock(&LOCK_prefetch_channel);
  return 0;
}

int ConsensusPreFetchChannel::disable_prefetch_channel() {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_channel);

  stop_prefetch_flag = true;
  mysql_cond_broadcast(&COND_prefetch_channel);

  mysql_mutex_unlock(&LOCK_prefetch_channel);
  return 0;
}

int ConsensusPreFetchChannel::start_prefetch_thread() {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_channel);
  stop_prefetch_flag = false;
  mysql_mutex_unlock(&LOCK_prefetch_channel);

  mysql_mutex_lock(&LOCK_prefetch_request);
  is_running = true;
  mysql_mutex_unlock(&LOCK_prefetch_request);

  if (mysql_thread_create(key_thread_prefetch, &prefetch_thread_handle, nullptr,
                          run_prefetch, (void *)this)) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_CREATE_THRERD_ERROR, "run_prefetch");
    abort();
  }

  return 0;
}

int ConsensusPreFetchChannel::stop_prefetch_thread() {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_request);
  is_running = false;
  mysql_cond_broadcast(&COND_prefetch_request);
  mysql_mutex_unlock(&LOCK_prefetch_request);

  mysql_mutex_lock(&LOCK_prefetch_channel);
  stop_prefetch_flag = true;
  mysql_cond_broadcast(&COND_prefetch_channel);
  mysql_mutex_unlock(&LOCK_prefetch_channel);

  my_thread_join(&prefetch_thread_handle, nullptr);
  return 0;
}

int ConsensusPreFetchChannel::truncate_prefetch_cache(uint64 index) {
  DBUG_TRACE;

  LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_LOG_PREFETCH_CACHE_TRUNCATE,
               "before", channel_id, first_index_in_cache.load(),
               prefetch_cache.size());

  /* truncate large_trx_table */
  mysql_mutex_lock(&LOCK_prefetch_channel);

  if (channel_id == 0) {
    auto it = large_trx_table.lower_bound(index);
    large_trx_table.erase(it, large_trx_table.end());
  }

  if (max_prefetch_cache_size == 0 || prefetch_cache.size() == 0) {
    mysql_mutex_unlock(&LOCK_prefetch_channel);
    return 0;
  }

  if (index <= first_index_in_cache) {
    for (auto iter = prefetch_cache.begin(); iter != prefetch_cache.end();
         ++iter) {
      if (iter->buf_size > 0) my_free(iter->buffer);
    }
    prefetch_cache.clear();
    first_index_in_cache = 0;
    prefetch_cache_size = 0;
  } else if ((index > first_index_in_cache) &&
             (index < first_index_in_cache + prefetch_cache.size())) {
    while (index < first_index_in_cache + prefetch_cache.size()) {
      ConsensusLogEntry old_log = prefetch_cache.back();
      if (old_log.buf_size > 0) my_free(old_log.buffer);
      prefetch_cache_size -= old_log.buf_size;
      prefetch_cache.pop_back();
    }
  }

  mysql_mutex_unlock(&LOCK_prefetch_channel);

  LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_LOG_PREFETCH_CACHE_TRUNCATE,
               "after", channel_id, first_index_in_cache.load(),
               prefetch_cache.size());
  return 0;
}

int ConsensusPreFetchChannel::set_prefetch_request(uint64 index) {
  DBUG_TRACE;

  if (index == 0) return 0;

  mysql_mutex_lock(&LOCK_prefetch_request);
  current_prefetch_request = index;
  mysql_cond_broadcast(&COND_prefetch_request);
  mysql_mutex_unlock(&LOCK_prefetch_request);

  return 0;
}

uint64 ConsensusPreFetchChannel::get_prefetch_request() {
  uint64 index = 0;
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_prefetch_request);
  index = current_prefetch_request;
  if (index == 0) {
    mysql_cond_wait(&COND_prefetch_request, &LOCK_prefetch_request);
  }
  mysql_mutex_unlock(&LOCK_prefetch_request);

  return index;
}

int ConsensusPreFetchChannel::clear_prefetch_request() {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_prefetch_request);
  current_prefetch_request = 0;
  mysql_mutex_unlock(&LOCK_prefetch_request);
  return 0;
}

int ConsensusPreFetchManager::init(uint64 max_prefetch_cache_size_arg) {
  DBUG_TRACE;
  mysql_rwlock_init(key_rwlock_ConsensusLog_prefetch_channels_hash,
                    &LOCK_prefetch_channels_hash);

  max_prefetch_cache_size = max_prefetch_cache_size_arg;
  prefetch_window_size = 10;
  prefetch_wakeup_ratio = 2;

  inited = true;

  return 0;
}

int ConsensusPreFetchManager::cleanup() {
  DBUG_TRACE;

  if (!inited) return 0;

  mysql_rwlock_destroy(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->cleanup();
    delete iter->second;
  }

  inited = false;
  return 0;
}

int ConsensusPreFetchManager::trunc_log_from_prefetch_cache(uint64 index) {
  DBUG_TRACE;
  mysql_rwlock_wrlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->truncate_prefetch_cache(index);
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

ConsensusPreFetchChannel *ConsensusPreFetchManager::get_prefetch_channel(
    uint64 channel_id) {
  ConsensusPreFetchChannel *channel = nullptr;
  DBUG_TRACE;

  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  auto it = prefetch_channels_hash.find(channel_id);
  if (it != prefetch_channels_hash.end()) {
    channel = it->second;
    channel->inc_ref_count();
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);

  if (channel == nullptr) {
    mysql_rwlock_wrlock(&LOCK_prefetch_channels_hash);

    it = prefetch_channels_hash.find(channel_id);
    if (it == prefetch_channels_hash.end()) {
      channel = new ConsensusPreFetchChannel();
      channel->init(channel_id, max_prefetch_cache_size, prefetch_window_size,
                    prefetch_wakeup_ratio);
      prefetch_channels_hash.insert(
          std::map<uint64, ConsensusPreFetchChannel *>::value_type(channel_id,
                                                                   channel));
    } else {
      channel = it->second;
    }
    channel->inc_ref_count();

    mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  }

  return channel;
}

int ConsensusPreFetchManager::reset_prefetch_cache() {
  int error = 0;
  DBUG_TRACE;

  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);

  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    error = iter->second->reset_prefetch_cache();
    if (error) break;
  }

  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);

  return error;
}

int ConsensusPreFetchManager::drop_prefetch_channel(uint64 channel_id) {
  int error = 0;
  ConsensusPreFetchChannel *channel = nullptr;
  DBUG_TRACE;

  mysql_rwlock_wrlock(&LOCK_prefetch_channels_hash);

  auto iter = prefetch_channels_hash.find(channel_id);
  if (iter == prefetch_channels_hash.end()) {
    error = 1;
  } else {
    channel = iter->second;
    prefetch_channels_hash.erase(channel_id);
  }

  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);

  if (!error) {
    channel->cleanup();
    delete channel;
  }
  return error;
}

int ConsensusPreFetchManager::set_max_prefetch_cache_size(
    uint64 max_prefetch_cache_size_arg) {
  DBUG_TRACE;
  max_prefetch_cache_size = max_prefetch_cache_size_arg;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->set_max_prefetch_cache_size(max_prefetch_cache_size_arg);
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

int ConsensusPreFetchManager::set_prefetch_window_size(
    uint64 prefetch_window_size_arg) {
  DBUG_TRACE;
  prefetch_window_size = prefetch_window_size_arg;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->set_window_size(prefetch_window_size_arg);
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

int ConsensusPreFetchManager::set_prefetch_wakeup_ratio(
    uint64 prefetch_wakeup_ratio_arg) {
  DBUG_TRACE;
  prefetch_wakeup_ratio = prefetch_wakeup_ratio_arg;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->set_wakeup_ratio(prefetch_wakeup_ratio_arg);
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

int ConsensusPreFetchManager::enable_all_prefetch_channels() {
  DBUG_TRACE;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->enable_prefetch_channel();
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

int ConsensusPreFetchManager::disable_all_prefetch_channels() {
  DBUG_TRACE;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->disable_prefetch_channel();
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

int ConsensusPreFetchManager::start_prefetch_threads() {
  DBUG_TRACE;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->start_prefetch_thread();
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

int ConsensusPreFetchManager::stop_prefetch_threads() {
  DBUG_TRACE;
  mysql_rwlock_rdlock(&LOCK_prefetch_channels_hash);
  for (auto iter = prefetch_channels_hash.begin();
       iter != prefetch_channels_hash.end(); ++iter) {
    iter->second->stop_prefetch_thread();
  }
  mysql_rwlock_unlock(&LOCK_prefetch_channels_hash);
  return 0;
}

void *run_prefetch(void *arg) {
  ConsensusPreFetchChannel *channel = (ConsensusPreFetchChannel *)arg;
  DBUG_TRACE;

  if (my_thread_init()) return nullptr;

  THD *thd = create_thd(false, true, true, channel->get_PSI_thread_key(), 0);
  while (channel->is_running) {
    uint64 index = channel->get_prefetch_request();
    if (index == 0) {
      continue;
    }

    if (!channel->log_exist(index)) {
      LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_LOG_RUN_PREFETCH,
                   channel->get_channel_id(), index);
      if (consensus_log_manager.prefetch_log_directly(
              thd, channel->get_channel_id(), index)) {
        channel->clear_prefetch_request();
      }
    } else {
      channel->clear_prefetch_request();
    }
  }

  destroy_thd(thd);
  my_thread_end();
  return nullptr;
}
