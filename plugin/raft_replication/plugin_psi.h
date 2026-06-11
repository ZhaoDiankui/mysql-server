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

#ifndef CONSENSUS_PLUGIN_PSI_INCLUDED
#define CONSENSUS_PLUGIN_PSI_INCLUDED

#include "mysys/mysys_priv.h"

#ifdef HAVE_PSI_INTERFACE
/*
  Register the psi keys for mutexes, conditions, threads and rwlocks
*/
void register_all_consensus_replication_psi_keys();
#endif /* HAVE_PSI_INTERFACE */

/* clang-format off */
extern PSI_mutex_key key_mutex_ConsensusLog_index;
extern PSI_mutex_key key_mutex_ConsensusLog_term_lock;
extern PSI_mutex_key key_mutex_ConsensusLog_apply_thread_lock;
extern PSI_mutex_key key_mutex_ConsensusLog_commit_advance_lock;
extern PSI_mutex_key key_mutex_Consensus_stage_change;

extern PSI_mutex_key key_fifo_cache_cleaner;

extern PSI_rwlock_key key_rwlock_plugin_running;
extern PSI_rwlock_key key_rwlock_plugin_stop;
extern PSI_rwlock_key key_rwlock_ConsensusLog_status_lock;
extern PSI_rwlock_key key_rwlock_ConsensusLog_commit_lock;
extern PSI_rwlock_key key_rwlock_ConsensusLog_truncate_lock;
extern PSI_rwlock_key key_rwlock_ConsensusLog_rotate_lock;
extern PSI_rwlock_key key_rwlock_ConsensusLog_log_cache_lock;
extern PSI_rwlock_key key_rwlock_ConsensusLog_prefetch_channels_hash;

extern PSI_cond_key key_COND_ConsensusLog_catchup;
extern PSI_cond_key key_COND_ConsensusLog_commit_advance;
extern PSI_cond_key key_COND_Consensus_state_change;
extern PSI_cond_key key_COND_prefetch_reuqest;

extern PSI_thread_key key_thread_consensus_stage_change;
extern PSI_thread_key key_thread_consensus_commit_advance;
extern PSI_thread_key key_thread_prefetch;
extern PSI_thread_key key_thread_cleaner;

extern PSI_memory_key key_memory_ConsensusLogManager;
extern PSI_memory_key key_memory_ConsensusPreFetchManager;

/* clang-format on */

#endif /* CONSENSUS_PLUGIN_PSI_INCLUDED */
