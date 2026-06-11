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

#include "consensus_applier_info.h"
#include "consensus_info_factory.h"

#include "mutex_lock.h"
#include "mysqld_error.h"

#include "sql/log.h"
#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"

#include "sql/current_thd.h"
#include "sql/handler.h"
#include "sql/mysqld.h"
#include "sql/rpl_rli.h"

using std::max;
using std::min;

const char *info_consensus_applier_fields[] = {
    "number_of_lines", "number_of_workers", "consensus_apply_index"};

Consensus_applier_info::Consensus_applier_info(
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
    )
    : Rpl_info("Consensus applier"
#ifdef HAVE_PSI_INTERFACE
               ,
               param_key_info_run_lock, param_key_info_data_lock,
               param_key_info_sleep_lock, param_key_info_thd_lock,
               param_key_info_data_cond, param_key_info_start_cond,
               param_key_info_stop_cond, param_key_info_sleep_cond
#endif
               ,
               0, ""),
      workers_array_initialized(false),
      workers(PSI_NOT_INSTRUMENTED),
      parallel_workers(0),
      recovery_parallel_workers(0),
      consensus_apply_index(0),
      mts_consensus_hwm_index(0) {
}

/**
Creates or reads information from the repository, initializing the
Consensus_info.
*/
int Consensus_applier_info::init_info() {
  enum_return_check check_return = ERROR_CHECKING_REPOSITORY;
  DBUG_TRACE;

  if (inited) return 0;

  mysql_mutex_init(key_LOCK_consensus_applier_info,
                   &LOCK_consensus_applier_info, MY_MUTEX_INIT_FAST);

  if ((check_return = check_info()) == ERROR_CHECKING_REPOSITORY) goto err;

  if (handler->init_info()) goto err;

  if (check_return != REPOSITORY_DOES_NOT_EXIST) {
    if (read_info(handler)) goto err;
  }

  inited = 1;
  if (flush_info(true, true)) goto err;

  return 0;
err:
  handler->end_info();
  inited = 0;
  LogErr(ERROR_LEVEL, ER_CONSENSUS_READ_METADATA_ERROR,
         "consensus_applier_info");
  abort();
  return 1;
}

void Consensus_applier_info::end_info() {
  DBUG_TRACE;

  if (!inited) return;

  mysql_mutex_destroy(&LOCK_consensus_applier_info);
  handler->end_info();
  inited = 0;
  return;
}

int Consensus_applier_info::flush_info(bool force, bool need_commit) {
  int error = 0;
  DBUG_TRACE;

  if (!inited) return 0;
  /*
  We update the sync_period at this point because only here we
  now that we are handling a master info. This needs to be
  update every time we call flush because the option maybe
  dinamically set.
  */
  mysql_mutex_lock(&LOCK_consensus_applier_info);

  if (write_info(handler))
    goto err;

  error = handler->flush_info(force);

  if (error) {
    if (force && need_commit && current_thd)
      ha_rollback_trans(current_thd, true);
    goto err;
  } else if (force && need_commit && current_thd) {
    error = ha_commit_trans(current_thd, true, true);
  }

  mysql_mutex_unlock(&LOCK_consensus_applier_info);
  return error;

err:
  mysql_mutex_unlock(&LOCK_consensus_applier_info);
  LogErr(ERROR_LEVEL, ER_CONSENSUS_WRITE_METADATA_ERROR,
         "consensus_applier_info");
  abort();
  return 1;
}

bool Consensus_applier_info::set_info_search_keys(Rpl_info_handler *to) {
  DBUG_TRACE;
  if (to->set_info(0, (int)get_number_fields()))
    return true;
  return false;
}

bool Consensus_applier_info::read_info(Rpl_info_handler *from) {
  char number_of_lines[FN_REFLEN] = {0};
  ulong temp_apply_index = 0;
  DBUG_TRACE;

  if (from->prepare_info_for_read() ||
      !!from->get_info(number_of_lines, sizeof(number_of_lines), ""))
    return true;

  if (!!from->get_info(&recovery_parallel_workers, 0) ||
      !!from->get_info(&temp_apply_index, 0))
    return true;

  consensus_apply_index = temp_apply_index;

  return false;
}

bool Consensus_applier_info::write_info(Rpl_info_handler *to) {
  DBUG_TRACE;
  if (to->prepare_info_for_write() || to->set_info((int)get_number_fields()) ||
      to->set_info(recovery_parallel_workers) ||
      to->set_info((ulong)consensus_apply_index))
    return true;
  return false;
}

size_t Consensus_applier_info::get_number_fields() {
  return sizeof(info_consensus_applier_fields) / sizeof(info_consensus_applier_fields[0]);
}

void Consensus_applier_info::set_nullable_fields(MY_BITMAP *nullable_fields) {
  bitmap_init(nullable_fields, nullptr,
              Consensus_applier_info::get_number_fields());
  bitmap_clear_all(nullable_fields);
}

int Consensus_applier_info::commit_positions(uint64 event_consensus_index,
                                             bool to_flush) {
  DBUG_TRACE;
  saved_consensus_apply_index = get_consensus_apply_index();
  set_consensus_apply_index(event_consensus_index);
  return to_flush ? flush_info(true) : 0;
}

int Consensus_applier_info::rollback_positions() {
  DBUG_TRACE;
  set_consensus_apply_index(saved_consensus_apply_index);
  return 0;
}


/**
   Reset recovery info from Worker info table and
   mark MTS recovery is completed.

   @return false on success true when @c reset_notified_checkpoint failed.
*/
bool Consensus_applier_info::mts_finalize_recovery() {
  bool ret = false;
  uint i;

  DBUG_TRACE;

  for (Consensus_applier_worker **it = workers.begin();
       !ret && it != workers.end(); ++it) {
    Consensus_applier_worker *w = *it;
    ret = w->reset_recovery_info();
  }

  for (i = recovery_parallel_workers; i > workers.size() && !ret; i--) {
    Consensus_applier_worker *w =
        Consensus_info_factory::create_consensus_applier_woker(i - 1, false);
    /*
      If an error occurs during the above create_consensus_applier_woker call,
      the newly created worker object gets deleted within the above function
      call itself and only nullptr is returned. Hence the following check has been
      added to verify that a valid worker object exists.
    */
    if (w) {
      ret = w->remove_info();
      delete w;
    } else {
      ret = true;
      goto err;
    }
  }
  recovery_parallel_workers = workers.size();

err:
  return ret;
}

static int consensus_applier_create_single_worker(
    Consensus_applier_info *applier_info, ulong i) {
  int error = 0;
  Consensus_applier_worker *w = nullptr;

  DBUG_TRACE;

  if (!(w = Consensus_info_factory::create_consensus_applier_woker(i, false))) {
    error = 1;
    goto err;
  }

  if (w->init_worker(i)) {
    error = 1;
    goto err;
  }

  assert(i == applier_info->workers.size());
  if (i >= applier_info->workers.size()) applier_info->workers.resize(i + 1);
  applier_info->workers[i] = w;
err:
  if (error && w) {
    delete w;
    if (applier_info->workers.size() == i + 1) applier_info->workers.erase(i);
  }
  return error;
}

int create_applier_workers(Relay_log_info *rli,
                           Consensus_applier_info *applier_info,
                           ulong n_workers) {
  int error = 0;

  DBUG_TRACE;

  if (n_workers == 0 && rli->mts_recovery_group_cnt == 0) {
    applier_info->workers.clear();
    goto end;
  }

  applier_info->workers.reserve(
      max(n_workers, applier_info->recovery_parallel_workers));
  applier_info->workers_array_initialized = true;

  for (uint i = 0; i < n_workers; i++) {
    if ((error = consensus_applier_create_single_worker(applier_info, i)))
      goto err;
    applier_info->parallel_workers++;
  }

end:
  // Effective end of the recovery right now when there is no gaps
  if (!error && rli->mts_recovery_group_cnt == 0) {
    if ((error = applier_info->mts_finalize_recovery())) {
      (void)Consensus_info_factory::reset_consensus_applier_workers(
          applier_info);
    }
    if (!error) error = applier_info->flush_info(true, true);
  }
err:
  return error;
}

void destory_applier_workers(Relay_log_info *rli,
                             Consensus_applier_info *applier_info) {
  if (applier_info->parallel_workers == 0) goto end;

  {
    MUTEX_LOCK(lock, &rli->data_lock);
    while (!applier_info->workers.empty()) {
      Consensus_applier_worker *w = applier_info->workers.back();
      // Free the current submode object
      applier_info->workers.pop_back();
      delete w;
    }
  }

end:
  applier_info->workers.clear();
  applier_info->parallel_workers = 0;
}