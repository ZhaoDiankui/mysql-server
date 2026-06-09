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

#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "consensus_applier_info.h"
#include "consensus_applier_worker.h"
#include "consensus_info.h"
#include "consensus_info_factory.h"

#include "lex_string.h"
#include "m_ctype.h"
#include "m_string.h"
#include "my_base.h"
#include "my_inttypes.h"
#include "my_psi_config.h"
#include "mysqld_error.h"

#include "sql/log.h"
#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"

#include "sql/consensus/consensus_info.h"
#include "sql/field.h"
#include "sql/handler.h"
#include "sql/mysqld.h"  // key_source_info_run_lock
#include "sql/rpl_info_table.h"         // Rpl_info_table
#include "sql/rpl_info_table_access.h"  // Rpl_info_table_access
#include "sql/system_variables.h"

Consensus_info_factory::struct_table_data
    Consensus_info_factory::consensus_table_data;
Consensus_info_factory::struct_file_data
    Consensus_info_factory::consensus_file_data;

Consensus_info_factory::struct_table_data
    Consensus_info_factory::consensus_applier_table_data;
Consensus_info_factory::struct_file_data
    Consensus_info_factory::consensus_applier_file_data;

Consensus_info_factory::struct_table_data
    Consensus_info_factory::consensus_applier_worker_table_data;
Consensus_info_factory::struct_file_data
    Consensus_info_factory::consensus_applier_worker_file_data;

Consensus_info *Consensus_info_factory::create_consensus_info() {
  Consensus_info *consensus_info = nullptr;
  Rpl_info_handler *handler_src = nullptr;
  Rpl_info_handler *handler_dest = nullptr;
  const char *msg =
      "Failed to allocate memory for the consensus info "
      "structure";
  DBUG_TRACE;

  if (!(consensus_info = new Consensus_info(
#ifdef HAVE_PSI_INTERFACE
            &key_consensus_info_run_lock, &key_consensus_info_data_lock,
            &key_consensus_info_sleep_lock, &key_consensus_info_thd_lock,
            &key_consensus_info_data_cond, &key_consensus_info_start_cond,
            &key_consensus_info_stop_cond, &key_consensus_info_sleep_cond
#endif
            )))
    goto err;
  if (Rpl_info_factory::init_repositories(
          consensus_table_data, consensus_file_data, INFO_REPOSITORY_TABLE,
          &handler_src, &handler_dest, &msg))
    goto err;
  if (handler_dest->get_rpl_info_type() != INFO_REPOSITORY_TABLE) {
    LogErr(ERROR_LEVEL, ER_RPL_REPO_SHOULD_BE_TABLE);
    goto err;
  }

  consensus_info->set_rpl_info_handler(handler_dest);
  if (consensus_info->set_info_search_keys(handler_dest)) goto err;
  delete handler_src;
  return consensus_info;
err:
  delete handler_src;
  delete handler_dest;
  if (consensus_info) {
    /*
    The handler was previously deleted so we need to remove
    any reference to it.
    */
    consensus_info->set_rpl_info_handler(nullptr);
    delete consensus_info;
  }
  LogErr(ERROR_LEVEL, ER_CONSENSUS_CREATE_METADATA_ERROR, "consensus_info", msg);
  return nullptr;
}

void Consensus_info_factory::init_consensus_repo_metadata() {
  consensus_table_data.n_fields =
      Consensus_info::get_number_info_consensus_fields();
  consensus_table_data.schema = MYSQL_SCHEMA_NAME.str;
  consensus_table_data.name = CONSENSUS_INFO_NAME.str;
  consensus_file_data.n_fields =
      Consensus_info::get_number_info_consensus_fields();
  my_stpcpy(consensus_file_data.name, "consensus_info");
  my_stpcpy(consensus_file_data.pattern, "consensus_info");
  consensus_file_data.name_indexed = false;
  Consensus_info::set_nullable_fields(&consensus_table_data.nullable_fields);
  Consensus_info::set_nullable_fields(&consensus_file_data.nullable_fields);
}

Consensus_applier_info *
Consensus_info_factory::create_consensus_applier_info() {
  Consensus_applier_info *consensus_applier_info = nullptr;
  Rpl_info_handler *handler_src = nullptr;
  Rpl_info_handler *handler_dest = nullptr;
  const char *msg =
      "Failed to allocate memory for the consensus applier info "
      "structure";
  DBUG_TRACE;

  if (!(consensus_applier_info = new Consensus_applier_info(
#ifdef HAVE_PSI_INTERFACE
            &key_consensus_info_run_lock, &key_consensus_info_data_lock,
            &key_consensus_info_sleep_lock, &key_consensus_info_thd_lock,
            &key_consensus_info_data_cond, &key_consensus_info_start_cond,
            &key_consensus_info_stop_cond, &key_consensus_info_sleep_cond
#endif
            )))
    goto err;
  if (Rpl_info_factory::init_repositories(
          consensus_applier_table_data, consensus_applier_file_data,
          INFO_REPOSITORY_TABLE, &handler_src, &handler_dest, &msg))
    goto err;
  if (handler_dest->get_rpl_info_type() != INFO_REPOSITORY_TABLE) {
    LogErr(ERROR_LEVEL, ER_RPL_REPO_SHOULD_BE_TABLE);
    goto err;
  }

  consensus_applier_info->set_rpl_info_handler(handler_dest);
  if (consensus_applier_info->set_info_search_keys(handler_dest)) goto err;
  delete handler_src;
  return consensus_applier_info;
err:
  delete handler_src;
  delete handler_dest;
  if (consensus_applier_info) {
    /*
    The handler was previously deleted so we need to remove
    any reference to it.
    */
    consensus_applier_info->set_rpl_info_handler(nullptr);
    delete consensus_applier_info;
  }
  LogErr(ERROR_LEVEL, ER_CONSENSUS_CREATE_METADATA_ERROR,
               "consensus_applier_info", msg);
  return nullptr;
}

void Consensus_info_factory::init_consensus_applier_repo_metadata() {
  consensus_applier_table_data.n_fields =
      Consensus_applier_info::get_number_fields();
  consensus_applier_table_data.schema = MYSQL_SCHEMA_NAME.str;
  consensus_applier_table_data.name = CONSENSUS_APLLIER_INFO_NAME.str;
  consensus_applier_file_data.n_fields =
      Consensus_applier_info::get_number_fields();
  my_stpcpy(consensus_applier_file_data.name, "consensus_applier_info");
  my_stpcpy(consensus_applier_file_data.pattern, "consensus_applier_info");
  consensus_applier_file_data.name_indexed = false;
  Consensus_applier_info::set_nullable_fields(
      &consensus_applier_table_data.nullable_fields);
  Consensus_applier_info::set_nullable_fields(
      &consensus_applier_file_data.nullable_fields);
}

Consensus_applier_worker *
Consensus_info_factory::create_consensus_applier_woker(uint worker_id,
                                                       bool on_recovery) {
  Consensus_applier_worker *consensus_applier_worker = nullptr;
  Rpl_info_handler *handler_src = nullptr;
  Rpl_info_handler *handler_dest = nullptr;
  const char *msg =
      "Failed to allocate memory for the consensus applier worker"
      "structure";
  DBUG_TRACE;

  if (!(consensus_applier_worker = new Consensus_applier_worker(
#ifdef HAVE_PSI_INTERFACE
            &key_consensus_info_run_lock, &key_consensus_info_data_lock,
            &key_consensus_info_sleep_lock, &key_consensus_info_thd_lock,
            &key_consensus_info_data_cond, &key_consensus_info_start_cond,
            &key_consensus_info_stop_cond, &key_consensus_info_sleep_cond,
#endif
            worker_id)))
    goto err;
  if (Rpl_info_factory::init_repositories(
          consensus_applier_worker_table_data, consensus_applier_worker_file_data,
          INFO_REPOSITORY_TABLE, &handler_src, &handler_dest, &msg))
    goto err;
  if (handler_dest->get_rpl_info_type() != INFO_REPOSITORY_TABLE) {
    LogErr(ERROR_LEVEL, ER_RPL_REPO_SHOULD_BE_TABLE);
    goto err;
  }

  consensus_applier_worker->set_rpl_info_handler(handler_dest);
  if (consensus_applier_worker->set_info_search_keys(handler_dest)) goto err;

  if (consensus_applier_worker->init_info(on_recovery)) goto err;

  delete handler_src;
  return consensus_applier_worker;
err:
  delete handler_src;
  delete handler_dest;
  if (consensus_applier_worker) {
    /*
    The handler was previously deleted so we need to remove
    any reference to it.
    */
    consensus_applier_worker->set_rpl_info_handler(nullptr);
    delete consensus_applier_worker;
  }
  LogErr(ERROR_LEVEL, ER_CONSENSUS_CREATE_METADATA_ERROR,
               "consensus_applier_worker", msg);
  return nullptr;
}

/**
   Delete all info from Worker info tables to render them useless in
   future MTS recovery, and indicate that in Coordinator info table.

   @retval false on success
   @retval true when a failure in deletion or writing to Coordinator table
   fails.
*/
bool Consensus_info_factory::reset_consensus_applier_workers(
    Consensus_applier_info *applier_info) {
  bool error = false;

  DBUG_TRACE;

  /*
    Skip the optimization check if the last value of the number of workers
    might not have been persisted
  */
  if (applier_info->recovery_parallel_workers == 0) return false;

  if (Rpl_info_table::do_reset_all_info(
          Consensus_applier_worker::get_number_fields(), MYSQL_SCHEMA_NAME.str,
          CONSENSUS_APLLIER_WORKER_NAME.str,
          &consensus_applier_worker_table_data.nullable_fields)) {
    error = true;
    LogErr(ERROR_LEVEL,
           ER_RPL_FAILED_TO_DELETE_FROM_REPLICA_WORKERS_INFO_REPOSITORY);
    goto err;
  }

  applier_info->recovery_parallel_workers = 0;

  if (applier_info->flush_info(true, true)) {
    error = true;
    LogErr(ERROR_LEVEL, ER_RPL_FAILED_TO_RESET_STATE_IN_REPLICA_INFO_REPOSITORY);
  }

  DBUG_EXECUTE_IF("mta_debug_reset_consensus_workers_fails", error = true;);

err:
  return error;
}

void Consensus_info_factory::init_consensus_applier_worker_repo_metadata() {
  consensus_applier_worker_table_data.n_fields =
      Consensus_applier_worker::get_number_fields();
  consensus_applier_worker_table_data.schema = MYSQL_SCHEMA_NAME.str;
  consensus_applier_worker_table_data.name = CONSENSUS_APLLIER_WORKER_NAME.str;
  consensus_applier_worker_table_data.n_pk_fields = 1;
  consensus_applier_worker_table_data.pk_field_indexes =
      Consensus_applier_worker::get_table_pk_field_indexes();
  consensus_applier_worker_file_data.n_fields =
      Consensus_applier_worker::get_number_fields();
  my_stpcpy(consensus_applier_worker_file_data.name,
            "consensus_applier_worker");
  my_stpcpy(consensus_applier_worker_file_data.pattern,
            "consensus_applier_worker");
  consensus_applier_worker_file_data.name_indexed = false;
  Consensus_applier_worker::set_nullable_fields(
      &consensus_applier_worker_table_data.nullable_fields);
  Consensus_applier_worker::set_nullable_fields(
      &consensus_applier_worker_file_data.nullable_fields);
}