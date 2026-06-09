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

#include "consensus_binlog_recovery.h"
#include "consensus_binlog.h"
#include "consensus_log_manager.h"
#include "consensus_meta.h"
#include "consensus_recovery_manager.h"
#include "consensus_state_process.h"
#include "system_variables.h"

#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/log.h"

#include "mysql/plugin.h"
#include "scope_guard.h"  // Scope_guard

#include "sql/binlog.h"
#include "sql/binlog/decompressing_event_object_istream.h"  // binlog::Decompressing_event_object_istream
#include "sql/binlog/recovery.h"  // binlog::Binlog_recovery
#include "sql/binlog_istream.h"
#include "sql/binlog_ostream.h"
#include "sql/log_event.h"
#include "sql/mysqld.h" // innodb_hton
#include "sql/rpl_rli.h"
#include "sql/xa/recovery.h"
#include "sql/xa/xid_extract.h"  // xa::XID_extractor

static const int MIN_XID_LIST_SIZE = 128;
static const int MAX_XID_LIST_SIZE = 1024 * 128;

static int consensus_ha_recover_with_binlog(
    ConsensusRecoveryManager *recovery_manager, Xid_commit_list *commit_list,
    Xa_state_list::list *binlog_xid_map) {
  xarecover_st info;
  Xa_state_list *se_xa_list;
  Xa_state_list xa_list{*binlog_xid_map};
  info.found_foreign_xids = info.found_my_xids = 0;
  info.commit_list = commit_list;
  info.dry_run = false;
  info.list = nullptr;
  DBUG_TRACE;

  // destruct se_xa_list when this function returns
  std::unique_ptr<MEM_ROOT> mem_root{nullptr};
  std::unique_ptr<Xa_state_list::allocator> map_alloc{nullptr};
  std::unique_ptr<Xa_state_list::list> se_xid_map{nullptr};
  std::unique_ptr<Xa_state_list> se_xids{nullptr};
  std::tie(mem_root, map_alloc, se_xid_map, se_xids) =
      Xa_state_list::new_instance();
  se_xa_list = se_xids.get();

  assert(total_ha_2pc > 1);

  if (info.commit_list) LogPluginErr(SYSTEM_LEVEL, ER_XA_STARTING_RECOVERY);

  for (info.len = MAX_XID_LIST_SIZE;
       info.list == nullptr && info.len > MIN_XID_LIST_SIZE; info.len /= 2) {
    info.list = new (std::nothrow) XA_recover_txn[info.len];
  }
  if (!info.list) {
    LogPluginErr(ERROR_LEVEL, ER_SERVER_OUTOFMEMORY,
                 static_cast<int>(info.len * sizeof(XID)));
    return 1;
  }
  auto clean_up_guard = create_scope_guard([&] { delete[] info.list; });

  /* fetch the transactions which are in the prepared state from storage
   * engine to info.xa_list */
  info.xa_list = se_xa_list;
  if (plugin_foreach(nullptr, xa::recovery::recover_prepared_in_tc_one_ht,
                     MYSQL_STORAGE_ENGINE_PLUGIN, &info)) {
    return 1;
  }

  // reconstrunct xa_list and commit_list by se_xid_map and binlog_xid_map
  (void)recovery_manager->reconstruct_binlog_trx_list(
      *commit_list, xa_list, *se_xid_map, *binlog_xid_map);
  info.xa_list = &xa_list;

  if (plugin_foreach(nullptr, xa::recovery::recover_one_ht,
                     MYSQL_STORAGE_ENGINE_PLUGIN, &info)) {
    return 1;
  }

  if (info.found_foreign_xids)
    LogPluginErr(WARNING_LEVEL, ER_XA_RECOVER_FOUND_XA_TRX,
                 info.found_foreign_xids);
  if (info.commit_list) LogPluginErr(SYSTEM_LEVEL, ER_XA_RECOVERY_DONE);

  return 0;
}

static int consensus_ha_recover_without_binlog() {
  xarecover_st info;
  Xa_state_list *xa_list;
  DBUG_TRACE;
  info.commit_list = nullptr;
  info.dry_run = false;
  info.list = nullptr;

  std::unique_ptr<MEM_ROOT> mem_root{nullptr};
  std::unique_ptr<Xa_state_list::allocator> map_alloc{nullptr};
  std::unique_ptr<Xa_state_list::list> xid_map{nullptr};
  std::unique_ptr<Xa_state_list> external_xids{nullptr};
  std::tie(mem_root, map_alloc, xid_map, external_xids) =
      Xa_state_list::new_instance();
  xa_list = external_xids.get();
  info.xa_list = xa_list;

  /* total_ha_2pc must be set */
  assert(total_ha_2pc > (ulong)opt_bin_log);
  if (total_ha_2pc <= (ulong)opt_bin_log) return 0;

  LogErr(SYSTEM_LEVEL, ER_XA_STARTING_RECOVERY);

  for (info.len = MAX_XID_LIST_SIZE;
       info.list == nullptr && info.len > MIN_XID_LIST_SIZE; info.len /= 2) {
    info.list = new (std::nothrow) XA_recover_txn[info.len];
  }
  if (!info.list) {
    LogErr(ERROR_LEVEL, ER_SERVER_OUTOFMEMORY,
           static_cast<int>(info.len * sizeof(XID)));
    return 1;
  }
  auto clean_up_guard = create_scope_guard([&] { delete[] info.list; });

  plugin_ref plugin = ha_lock_engine(nullptr, innodb_hton);
  if (!plugin || xa::recovery::recover_prepared_in_tc_one_ht(
                     nullptr, plugin, &info)) {
    plugin_unlock(nullptr, plugin);
    return 1;
  }
  plugin_unlock(nullptr, plugin);

  if (plugin_foreach(nullptr, xa::recovery::inverse_recover_one_ht,
                     MYSQL_STORAGE_ENGINE_PLUGIN, &info)) {
    return 1;
  }

  LogErr(SYSTEM_LEVEL, ER_XA_RECOVERY_DONE);
  return 0;
}

class Consensus_binlog_recovery : public binlog::Binlog_recovery {
 public:
  Consensus_binlog_recovery(Binlog_file_reader &binlog_file_reader)
      : binlog::Binlog_recovery(binlog_file_reader) {}
  ~Consensus_binlog_recovery(){}

  Consensus_binlog_recovery &consensus_recover(bool ha_recover,
                                               bool has_ha_recover_end,
                                               uint64 ha_recover_end_index,
                                               bool log_recover_only);

  uint64 get_valid_index() const { return this->valid_index; }

 private:
  ConsensusRecoveryManager recovery_manager;
  bool begin_consensus{false};
  uint64 current_term{0};
  uint64 current_index{0};
  uint64 current_length{0};
  uint64 valid_index{0};
  uint current_flag{0};
  my_off_t start_pos{BIN_LOG_HEADER_SIZE};
  my_off_t end_pos{BIN_LOG_HEADER_SIZE};
  uint32 current_ev_ts{0};

  void after_process_xa_prepare_event(XA_prepare_log_event const &ev);
  void after_process_xid_event(Xid_log_event const &ev);
  void after_process_query_event(Query_log_event const &ev);
  void after_process_atomic_ddl(Query_log_event const &ev);
  void after_process_xa_commit_or_rollback(std::string const &query);

  void process_consensus_log_event(Consensus_log_event const &ev);
  void process_previous_consensus_index_event(
      Previous_consensus_index_log_event const &ev);
};

void Consensus_binlog_recovery::process_consensus_log_event(
    Consensus_log_event const &ev) {
  DBUG_TRACE;
  if (this->current_index > ev.get_index()) {
    this->m_is_malformed = true;
    this->m_failure_message.assign(
        "Consensus_log_event log index out of order");
    return;
  }

  if (this->end_pos < this->start_pos) {
    this->m_is_malformed = true;
    this->m_failure_message.assign("Consensus_log_event log struct broken");
    return;
  }

  if (this->current_term > 0 && ev.get_term() > this->current_term) {
    this->m_internal_xids.clear();
  }

  this->current_index = ev.get_index();
  this->current_term = ev.get_term();
  this->current_length = ev.get_length();
  this->current_flag = ev.get_flag();
  this->current_ev_ts = ev.common_header->when.tv_sec;
  this->start_pos = this->m_reader.position();
  this->end_pos = this->m_reader.position();

  this->begin_consensus = true;
}

void Consensus_binlog_recovery::process_previous_consensus_index_event(
    Previous_consensus_index_log_event const &ev) {
  DBUG_TRACE;
  this->current_index = ev.get_index() - 1;
  this->current_ev_ts = ev.common_header->when.tv_sec;
}

void Consensus_binlog_recovery::after_process_query_event(
    Query_log_event const &ev) {
  std::string query{ev.query};
  DBUG_TRACE;

  if (is_atomic_ddl_event(&ev))
    this->after_process_atomic_ddl(ev);

  else if (query.find("XA COMMIT") == 0)
    this->after_process_xa_commit_or_rollback(query);

  else if (query.find("XA ROLLBACK") == 0)
    this->after_process_xa_commit_or_rollback(query);
}

void Consensus_binlog_recovery::after_process_xid_event(
    Xid_log_event const &ev) {
  recovery_manager.add_trx_to_binlog_map(ev.xid, this->current_index,
                                         this->current_term);
}

void Consensus_binlog_recovery::after_process_xa_prepare_event(
    XA_prepare_log_event const &ev) {
  XID xid;
  DBUG_TRACE;
  xid = ev.get_xid();
  recovery_manager.add_trx_to_binlog_xa_map(&xid, false, this->current_index,
                                            this->current_term);
}

void Consensus_binlog_recovery::after_process_atomic_ddl(
    Query_log_event const &ev) {
  DBUG_TRACE;
  recovery_manager.add_trx_to_binlog_map(ev.ddl_xid, this->current_index,
                                         this->current_term);
}

void Consensus_binlog_recovery::after_process_xa_commit_or_rollback(
    std::string const &query) {
  DBUG_TRACE;
  xa::XID_extractor tokenizer{query, 1};

  assert(tokenizer.size() > 0);

  recovery_manager.add_trx_to_binlog_xa_map(
      &tokenizer[0], true, this->current_index, this->current_term);
}

Consensus_binlog_recovery &Consensus_binlog_recovery::consensus_recover(
    bool ha_recover, bool has_ha_recover_end, uint64 ha_recover_end_index,
    bool log_recover_only) {
  bool in_large_trx = false;
  bool ha_recover_end = false;
  uint64 start_index = 0;
  DBUG_TRACE;
  binlog::Decompressing_event_object_istream istream{this->m_reader};
  std::shared_ptr<Log_event> ev;
  this->m_valid_pos = this->m_reader.position();

  assert(!log_recover_only || !ha_recover);

  while (istream >> ev) {
    switch (ev->get_type_code()) {
      case binary_log::QUERY_EVENT: {
        if (ha_recover && !ha_recover_end) {
          this->process_query_event(dynamic_cast<Query_log_event &>(*ev));
          this->after_process_query_event(dynamic_cast<Query_log_event &>(*ev));
        }
        break;
      }
      case binary_log::XID_EVENT: {
        if (ha_recover && !ha_recover_end) {
          this->process_xid_event(dynamic_cast<Xid_log_event &>(*ev));
          this->after_process_xid_event(dynamic_cast<Xid_log_event &>(*ev));
        }
        break;
      }
      case binary_log::XA_PREPARE_LOG_EVENT: {
        if (ha_recover && !ha_recover_end) {
          this->process_xa_prepare_event(
              dynamic_cast<XA_prepare_log_event &>(*ev));
          this->after_process_xa_prepare_event(
              dynamic_cast<XA_prepare_log_event &>(*ev));
        }
        break;
      }
      case binary_log::CONSENSUS_LOG_EVENT: {
        this->process_consensus_log_event(
            dynamic_cast<Consensus_log_event &>(*ev));
        this->begin_consensus = true;
        if (start_index > 0) {
          update_pos_map_by_start_index(
              consensus_log_manager.get_log_file_index(), start_index,
              dynamic_cast<Consensus_log_event *>(ev.get()),
              this->m_reader.event_start_pos());
        }
        break;
      }
      case binary_log::PREVIOUS_CONSENSUS_INDEX_LOG_EVENT: {
        this->process_previous_consensus_index_event(
            dynamic_cast<Previous_consensus_index_log_event &>(*ev));
        start_index = this->current_index + 1;
        break;
      }
      default: {
        break;
      }
    }

    if (!this->m_is_malformed && !ev->is_control_event()) {
      this->end_pos = this->m_reader.position();
      /* find a integrated consensus log */
      if (this->begin_consensus && this->end_pos > this->start_pos &&
          this->end_pos - this->start_pos == this->current_length &&
          !(this->current_flag & Consensus_log_event_flag::FLAG_BLOB)) {
        in_large_trx =
            (this->current_flag & Consensus_log_event_flag::FLAG_LARGE_TRX);
        this->begin_consensus = false;
      }
    }

    if (!this->m_is_malformed && !this->begin_consensus &&
        !is_gtid_event(ev.get()) && !is_session_control_event(ev.get())) {
      this->valid_index = this->current_index;
      this->m_valid_pos = this->m_reader.position();
    }

    this->m_is_malformed = istream.has_error() || this->m_is_malformed;
    if (this->m_is_malformed) break;

    if (ha_recover_end_index > 0 && this->valid_index >= ha_recover_end_index) {
      ha_recover_end = true;
    }
  }

  if (istream.has_error()) {
    using Status_t = binlog::Decompressing_event_object_istream::Status_t;
    switch (istream.get_status()) {
      case Status_t::corrupted:
      case Status_t::out_of_memory:
      case Status_t::exceeds_max_size:
        // @todo Uncomment this to fix BUG#34828252
        // this->m_is_malformed = true;
        // this->m_failure_message.assign(istream.get_error_str());
        // break;
      case Status_t::success:
      case Status_t::end:
      case Status_t::truncated:
        // not malformed, just truncated
        break;
    }
  }

  assert(total_ha_2pc > 1);
  if (!this->m_is_malformed && ha_recover) {
    this->m_no_engine_recovery = consensus_ha_recover_with_binlog(
        &recovery_manager, &this->m_internal_xids, &this->m_external_xids);
    if (this->m_no_engine_recovery) {
      this->m_failure_message.assign("Recovery failed in storage engines");
    }
  }

  if (this->has_failures()) return (*this);

  if (ha_recover) {
    consensus_state_process.set_recovery_index_hwl(
        recovery_manager.max_committed_index > 0
            ? recovery_manager.max_committed_index
            : start_index - 1);
  } else if (has_ha_recover_end) {
    assert(!opt_cluster_log_type_instance && ha_recover_end_index == 0);
    consensus_state_process.set_recovery_index_hwl(0);
  } else if (!log_recover_only) {
    consensus_state_process.set_recovery_index_hwl(0);
    consensus_state_process.set_recovery_ignored(true);
    consensus_state_process.set_recovery_term(this->current_term);
  }

  consensus_log_manager.set_cache_index(this->valid_index);
  consensus_log_manager.set_sync_index(this->valid_index);
  consensus_log_manager.set_current_index(this->valid_index + 1);
  consensus_log_manager.set_in_large_trx(in_large_trx);
  consensus_log_manager.set_event_timestamp(this->current_ev_ts);

  LogPluginErr(
      SYSTEM_LEVEL, ER_CONSENSUS_LOG_RECOVERY_FINISHED,
      recovery_manager.max_committed_index, this->valid_index,
      log_recover_only ? 0 : consensus_state_process.get_recovery_index_hwl());

  return (*this);
}

int consensus_binlog_recovery(MYSQL_BIN_LOG *binlog, bool has_ha_recover_end,
                              uint64 ha_recover_end_index,
                              char *binlog_end_file,
                              my_off_t *binlog_end_pos) {
  LOG_INFO log_info;
  Log_event *ev = nullptr;
  int error = 0;
  bool should_retrieve_logs_end = false;
  char log_name[FN_REFLEN];
  const char *ha_recover_file = nullptr;
  std::string ha_recover_file_str;
  my_off_t valid_pos = 0;
  my_off_t binlog_size = 0;
  bool should_ha_recover;
  Binlog_file_reader binlog_file_reader(opt_source_verify_checksum);

  DBUG_TRACE;

  if (has_ha_recover_end && ha_recover_end_index > 0) {
    uint64 start_index;  // unused
    ConsensusLogIndex *log_file_index =
        consensus_log_manager.get_log_file_index();
    if (consensus_find_log_by_index(log_file_index, ha_recover_end_index,
                                    ha_recover_file_str, start_index)) {
      LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_FIND_LOG_ERROR,
                   ha_recover_end_index, "recovering");
      assert(log_file_index->get_first_log_file_name().empty());
      ha_recover_end_index = 0;
      ha_recover_file = log_file_index->get_first_log_file_name().c_str();
    } else {
      ha_recover_file = ha_recover_file_str.c_str();
    }
    LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_BINLOG_RECOVERING_POINT,
                 ha_recover_file, ha_recover_end_index,
                 "for consistent recovering");
  }

  if ((error = binlog->find_log_pos(&log_info, ha_recover_file,
                                    true /*need_lock_index=true*/))) {
    if (ha_recover_file != nullptr || error != LOG_INFO_EOF)
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
    else {
      /* should execute ha_recover */
      error = ha_recover();
      if (error)
        LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_ERROR_RETURNED_SE);
    }
    return error;
  }

  if (ha_recover_file != nullptr) {
    strmake(log_name, log_info.log_file_name, sizeof(log_name) - 1);
    /* Should retrieve latest log file if ha_recover_file is not the latest
     */
    error = binlog->find_next_log(&log_info, true /*need_lock_index=true*/);
    if (!error) should_retrieve_logs_end = true;
  } else {
    do {
      strmake(log_name, log_info.log_file_name, sizeof(log_name) - 1);
    } while (!(error = binlog->find_next_log(&log_info,
                                             true /*need_lock_index=true*/)));
    if (error != LOG_INFO_EOF) {
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
      return 1;
    }
  }

  if (binlog_file_reader.open(log_name)) {
    LogPluginErr(ERROR_LEVEL, ER_BINLOG_FILE_OPEN_FAILED,
                 binlog_file_reader.get_error_str());
    return 1;
  }

  ev = binlog_file_reader.read_event_object();
  if (!ev) {
    my_off_t binlog_size = binlog_file_reader.ifile()->length();
    LogPluginErr(ERROR_LEVEL, ER_READ_LOG_EVENT_FAILED,
                 binlog_file_reader.get_error_str(), binlog_size,
                 binary_log::UNKNOWN_EVENT);
    return 1;
  }

  if (ev->get_type_code() != binary_log::FORMAT_DESCRIPTION_EVENT) {
    LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_MALFORMED_LOG, log_name,
                 0, binlog_file_reader.position(),
                 Log_event::get_type_str(ev->get_type_code()));
    delete ev;
    return 1;
  }

  /*
    If the binary log was not properly closed it means that the server
    may have crashed. In that case, we need to call
    binlog::Binlog_recovery::recover()
    to:

      a) collect logged XIDs;
      b) complete the 2PC of the pending XIDs;
      c) collect the last valid position.

    Therefore, we do need to iterate over the binary log, even if
    total_ha_2pc == 1, to find the last valid group of events written.
    Later we will take this value and truncate the log if need be.
  */
  should_ha_recover =
      (!opt_cluster_log_type_instance &&
       ((has_ha_recover_end && ha_recover_end_index > 0) ||  // consist recovery
        (!has_ha_recover_end &&
         (ev->common_header->flags & LOG_EVENT_BINLOG_IN_USE_F ||
          DBUG_EVALUATE_IF("eval_force_bin_log_recovery", true, false)))));

  delete ev;

  LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_BINLOG_RECOVERING_AFTER_CRASH_USING,
               log_name, should_ha_recover);

  Consensus_binlog_recovery bl_recovery{binlog_file_reader};
  bl_recovery.consensus_recover(should_ha_recover, has_ha_recover_end, ha_recover_end_index, false);
  valid_pos = bl_recovery.get_valid_pos();
  binlog_size = binlog_file_reader.ifile()->length();

  if (bl_recovery.is_binlog_malformed()) {
    LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_MALFORMED_LOG, log_name,
                 valid_pos, binlog_file_reader.position(),
                 bl_recovery.get_failure_message().data());
    return 1;
  }

  if (has_ha_recover_end && ha_recover_end_index > 0 &&
      bl_recovery.get_valid_index() < ha_recover_end_index) {
    LogPluginErr(ERROR_LEVEL, ER_CONSENSUS_CONSIST_RECOVERY_INVALID_POSITION,
                 log_name, ha_recover_end_index, bl_recovery.get_valid_index(),
                 valid_pos, binlog_size);
    return 1;
  }

  if (bl_recovery.has_engine_recovery_failed()) {
    LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_ERROR_RETURNED_SE);
    if (!should_retrieve_logs_end && valid_pos > 0)
      (void)truncate_binlog_file_to_valid_pos(log_name, valid_pos, binlog_size,
                                              true);
    return 1;
  }

  /* Recovery consensus log status if needed when PITR */
  if (should_retrieve_logs_end) {
    binlog_file_reader.close();

    /* Retrieve latest log file */
    do {
      strmake(log_name, log_info.log_file_name, sizeof(log_name) - 1);
    } while (!(error = binlog->find_next_log(&log_info,
                                             true /*need_lock_index=true*/)));

    if (error != LOG_INFO_EOF) {
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
      return 1;
    }

    if (binlog_file_reader.open(log_name)) {
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_FILE_OPEN_FAILED,
                   binlog_file_reader.get_error_str());
      return 1;
    }

    ev = binlog_file_reader.read_event_object();
    if (!ev) {
      my_off_t binlog_size = binlog_file_reader.ifile()->length();
      LogPluginErr(ERROR_LEVEL, ER_READ_LOG_EVENT_FAILED,
                   binlog_file_reader.get_error_str(), binlog_size,
                   binary_log::UNKNOWN_EVENT);
      return 1;
    }

    if (ev->get_type_code() != binary_log::FORMAT_DESCRIPTION_EVENT) {
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_MALFORMED_LOG,
                   log_name, 0, binlog_file_reader.position(),
                   Log_event::get_type_str(ev->get_type_code()));
      delete ev;
      return 1;
    }

    delete ev;

    LogPluginErr(SYSTEM_LEVEL, ER_CONSENSUS_BINLOG_RECOVERING_USING, log_name);

    /* Scan the log file and recovery consensus log status */
    Consensus_binlog_recovery bl_scan{binlog_file_reader};
    bl_scan.consensus_recover(false, false, 0, true);
    valid_pos = bl_scan.get_valid_pos();
    binlog_size = binlog_file_reader.ifile()->length();

    if (bl_scan.is_binlog_malformed()) {
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_MALFORMED_LOG,
                   log_name, valid_pos, binlog_file_reader.position(),
                   bl_scan.get_failure_message().data());
      return 1;
    }
  }

  /* Trim the crashed binlog file to last valid transaction
    or event (non-transaction) base on valid_pos. */
  if (valid_pos > 0) {
    if (binlog_end_file != nullptr && binlog_end_pos != nullptr) {
      strncpy(binlog_end_file, log_name, FN_REFLEN);
      *binlog_end_pos = valid_pos;
    }
    error = truncate_binlog_file_to_valid_pos(log_name, valid_pos, binlog_size,
                                              true);
  }

  if (!should_ha_recover) {
    error = consensus_ha_recover_without_binlog();
    if (error)
      LogPluginErr(ERROR_LEVEL, ER_BINLOG_CRASH_RECOVERY_ERROR_RETURNED_SE);
  }

  return error;
}
