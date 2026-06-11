/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited
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

#include "plugin_psi.h"

#include "template_utils.h"
#include "mysql/psi/mysql_cond.h"
#include "mysql/psi/mysql_memory.h"
#include "mysql/psi/mysql_rwlock.h"
#include "mysql/psi/mysql_stage.h"
#include "mysql/psi/mysql_thread.h"

/* clang-format off */

PSI_mutex_key key_mutex_ConsensusLog_index;
PSI_mutex_key key_mutex_ConsensusLog_term_lock;
PSI_mutex_key key_mutex_ConsensusLog_apply_thread_lock;
PSI_mutex_key key_mutex_ConsensusLog_commit_advance_lock;
PSI_mutex_key key_mutex_Consensus_stage_change;
PSI_mutex_key key_fifo_cache_cleaner;

PSI_rwlock_key key_rwlock_plugin_running;
PSI_rwlock_key key_rwlock_plugin_stop;
PSI_rwlock_key key_rwlock_ConsensusLog_status_lock;
PSI_rwlock_key key_rwlock_ConsensusLog_commit_lock;
PSI_rwlock_key key_rwlock_ConsensusLog_truncate_lock;
PSI_rwlock_key key_rwlock_ConsensusLog_rotate_lock;
PSI_rwlock_key key_rwlock_ConsensusLog_log_cache_lock;
PSI_rwlock_key key_rwlock_ConsensusLog_prefetch_channels_hash;

PSI_cond_key key_COND_ConsensusLog_catchup;
PSI_cond_key key_COND_Consensus_state_change;
PSI_cond_key key_COND_ConsensusLog_commit_advance;
PSI_cond_key key_COND_prefetch_reuqest;

PSI_thread_key key_thread_consensus_stage_change;
PSI_thread_key key_thread_consensus_commit_advance;
PSI_thread_key key_thread_prefetch;
PSI_thread_key key_thread_cleaner;

PSI_memory_key key_memory_ConsensusLogManager;
PSI_memory_key key_memory_ConsensusPreFetchManager;

/* clang-format on */

#ifdef HAVE_PSI_INTERFACE

static PSI_mutex_info all_consensus_replication_psi_mutex_keys[] = {
    {&key_mutex_ConsensusLog_index,
     "ConsensusLogIndex::LOCK_consensuslog_index", 0, 0, PSI_DOCUMENT_ME},
    {&key_mutex_ConsensusLog_term_lock,
     "ConsensusLogManager::LOCK_consensuslog_term", 0, 0, PSI_DOCUMENT_ME},
    {&key_mutex_ConsensusLog_apply_thread_lock,
     "ConsensusLogManager::LOCK_consensuslog_apply_thread_lock", 0, 0,
     PSI_DOCUMENT_ME},
    {&key_mutex_ConsensusLog_commit_advance_lock,
     "ConsensusLogManager::LOCK_consensuslog_commit_advance_lock", 0, 0,
     PSI_DOCUMENT_ME},
    {&key_mutex_Consensus_stage_change,
     "ConsensusLogManager::LOCK_consnesus_state_change", 0, 0, PSI_DOCUMENT_ME},
};

static PSI_cond_info all_consensus_replication_psi_condition_keys[] = {
    {&key_COND_ConsensusLog_catchup,
     "ConsensusLogManager::cond_consensuslog_catchup", 0, 0, PSI_DOCUMENT_ME},
    {&key_COND_ConsensusLog_commit_advance,
     "ConsensusLogManager::cond_consensuslog_commit_advance", 0, 0,
     PSI_DOCUMENT_ME},
    {&key_COND_Consensus_state_change,
     "ConsensusLogManager::cond_consensus_state_change", 0, 0, PSI_DOCUMENT_ME},
    {&key_COND_prefetch_reuqest,
     "ConsensusPrefetchManager::cond_prefetch_reuqest", 0, 0, PSI_DOCUMENT_ME},
};

static PSI_thread_info all_consensus_replication_psi_thread_keys[] = {
    {&key_thread_consensus_stage_change, "consensus_stage_change",
     "cons_stg_change", PSI_FLAG_SINGLETON, 0, PSI_DOCUMENT_ME},
    {&key_thread_consensus_commit_advance, "consensus_commit_advance",
     "cons_stg_change", PSI_FLAG_SINGLETON, 0, PSI_DOCUMENT_ME},
    {&key_thread_prefetch, "run_prefetch", "prefetch", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_thread_cleaner, "fifo_cleaner", "fifo_cleaner", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME}};

static PSI_rwlock_info all_consensus_replication_psi_rwlock_keys[] = {
    {&key_rwlock_plugin_running, "rwlock_plugin_running", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_plugin_stop, "rwlock_plugin_stop", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_ConsensusLog_status_lock,
     "ConsensusLogManager::LOCK_consensuslog_sequence", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_ConsensusLog_commit_lock,
     "ConsensusLogManager::LOCK_consensuslog_commit", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_ConsensusLog_truncate_lock,
     "ConsensusLogManager::LOCK_consensuslog_truncate", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_ConsensusLog_rotate_lock,
     "ConsensusLogManager::LOCK_consensuslog_rotate", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_ConsensusLog_log_cache_lock,
     "ConsensusLogManager::LOCK_consensuslog_cache", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_rwlock_ConsensusLog_prefetch_channels_hash,
     "ConsensusPreFetchManager::LOCK_prefetch_channels_hash",
     PSI_FLAG_SINGLETON, 0, PSI_DOCUMENT_ME},

};

static PSI_memory_info all_consensus_replication_psi_memory_keys[] = {
    {&key_memory_ConsensusLogManager, "ConsensusLogManager", 0, 0,
     PSI_DOCUMENT_ME},
    {&key_memory_ConsensusPreFetchManager, "ConsensusPreFetchManager", 0, 0,
     PSI_DOCUMENT_ME},
};

static void register_consensus_replication_mutex_psi_keys(
    PSI_mutex_info mutexes[], size_t mutex_count) {
  const char *category = "cons_rpl";
  if (mutexes != nullptr) {
    mysql_mutex_register(category, mutexes, static_cast<int>(mutex_count));
  }
}

static void register_consensus_replication_cond_psi_keys(PSI_cond_info conds[],
                                                         size_t cond_count) {
  const char *category = "cons_rpl";
  if (conds != nullptr) {
    mysql_cond_register(category, conds, static_cast<int>(cond_count));
  }
}

static void register_consensus_replication_thread_psi_keys(
    PSI_thread_info threads[], size_t thread_count) {
  const char *category = "cons_rpl";
  if (threads != nullptr) {
    mysql_thread_register(category, threads, static_cast<int>(thread_count));
  }
}

static void register_consensus_replication_rwlock_psi_keys(
    PSI_rwlock_info *keys, size_t count) {
  const char *category = "cons_rpl";
  mysql_rwlock_register(category, keys, static_cast<int>(count));
}

/*
  Register the psi keys for memory

  @param[in]  keys           PSI memory info
  @param[in]  count          The number of elements in keys
*/
void register_consensus_replication_memory_psi_keys(PSI_memory_info *keys,
                                                size_t count) {
  const char *category = "cons_rpl";
  mysql_memory_register(category, keys, static_cast<int>(count));
}

void register_all_consensus_replication_psi_keys() {
  register_consensus_replication_mutex_psi_keys(
      all_consensus_replication_psi_mutex_keys,
      array_elements(all_consensus_replication_psi_mutex_keys));
  register_consensus_replication_cond_psi_keys(
      all_consensus_replication_psi_condition_keys,
      array_elements(all_consensus_replication_psi_condition_keys));
  register_consensus_replication_thread_psi_keys(
      all_consensus_replication_psi_thread_keys,
      array_elements(all_consensus_replication_psi_thread_keys));
  register_consensus_replication_rwlock_psi_keys(
      all_consensus_replication_psi_rwlock_keys,
      array_elements(all_consensus_replication_psi_rwlock_keys));
  register_consensus_replication_memory_psi_keys(
      all_consensus_replication_psi_memory_keys,
      array_elements(all_consensus_replication_psi_memory_keys));
}

#endif
