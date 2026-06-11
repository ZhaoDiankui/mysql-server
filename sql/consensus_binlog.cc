/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited
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

#include "sql/binlog.h"

#include "my_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include "my_dbug.h"
// #include "mysql/components/services/log_builtins.h"
#include "sql/binlog.h"
#include "sql/debug_sync.h"  // DEBUG_SYNC
#include "sql/log.h"
#include "sql/sql_lex.h"
#include "sql/tztime.h"

bool MYSQL_BIN_LOG::open_for_consensus(
#ifdef HAVE_PSI_INTERFACE
    PSI_file_key log_file_key,
#endif
    const char *log_name, bool set_flags) {
  my_off_t file_off = 0;
  MY_STAT info;
  uchar new_flags;
  DBUG_TRACE;

  write_error = 0;
  myf flags = MY_WME | MY_NABP | MY_WAIT_IF_FULL;

  if (!(name = my_strdup(key_memory_MYSQL_LOG_name, log_name, MYF(MY_WME)))) {
    return true;
  }

  db[0] = 0;

  /* Keep the key for reopen */
  m_log_file_key = log_file_key;

  /*
    LOCK_sync guarantees that no thread is calling m_binlog_file to sync data
    to disk when another thread is opening the new file
    (FLUSH LOG or RESET MASTER).
  */
  mysql_mutex_lock(&LOCK_sync);

  std::unique_ptr<Binlog_ofile> ofile(
      m_binlog_file->open_existing(log_file_key, log_file_name, flags));
  if (ofile == nullptr) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING, name, errno);
    goto err;
  }

  /* Set LOG_EVENT_BINLOG_IN_USE_F */
  if (set_flags) {
    new_flags = is_consensus_write ? 0 : 1;
    if (ofile->update(&new_flags, 1, BIN_LOG_HEADER_SIZE + FLAGS_OFFSET)) {
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_CLEAR_IN_USE_FLAG_FOR_CRASHED_BINLOG);
      goto err;
    }
    ofile->flush_and_sync();
  }

  m_binlog_file = ofile.release();

  // write from the end
  if (!mysql_file_stat(log_file_key, log_file_name, &info, MYF(0))) {
    assert(!(my_errno() == ENOENT));
    goto err;
  }
  file_off = !m_binlog_file->is_encrypted()
                 ? info.st_size
                 : info.st_size - m_binlog_file->get_encrypted_header_size();
  m_binlog_file->seek(file_off);

  mysql_mutex_unlock(&LOCK_sync);

  atomic_log_state = LOG_OPENED;
  return false;

err:
  mysql_mutex_unlock(&LOCK_sync);
  if (binlog_error_action == ABORT_SERVER) {
    exec_binlog_error_action_abort(
        "Either disk is full, file system is read only or "
        "there was an encryption error while opening the binlog. "
        "Aborting the server.");
  } else
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING, log_name, errno);

  my_free(name);
  name = nullptr;
  atomic_log_state = LOG_CLOSED;
  return true;
}

int MYSQL_BIN_LOG::get_file_names(std::vector<std::string> &file_name_vector) {
  LOG_INFO log_info;
  int error = 1;
  mysql_mutex_lock(&LOCK_index);
  // find last log name according to the binlog index
  if (!my_b_inited(&index_file)) {
    mysql_mutex_unlock(&LOCK_index);
    return 1;
  }
  if ((error = find_log_pos(&log_info, NullS, false))) {
    if (error != LOG_INFO_EOF) {
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
      mysql_mutex_unlock(&LOCK_index);
      return error;
    }
  }
  if (error == 0) {
    do {
      file_name_vector.push_back(log_info.log_file_name);
    } while (
        !(error = find_next_log(&log_info, false /*need_lock_index=true*/)));
  }
  if (error != LOG_INFO_EOF) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
    mysql_mutex_unlock(&LOCK_index);
    return error;
  } else {
    error = 0;
  }
  mysql_mutex_unlock(&LOCK_index);

  return error;
}

int MYSQL_BIN_LOG::truncate_all_files() {
  int error = 0;
  LOG_INFO linfo;
  const char *save_name = nullptr;

  // modify index file
  mysql_mutex_lock(&LOCK_index);
  save_name = name;
  name = nullptr;
  close(LOG_CLOSE_TO_BE_OPENED, false, false);

  /*
    First delete all old log files and then update the index file.
    As we first delete the log files and do not use sort of logging,
    a crash may lead to an inconsistent state where the index has
    references to non-existent files.

    We need to invert the steps and use the purge_index_file methods
    in order to make the operation safe.
  */
  if ((error = find_log_pos(&linfo, NullS, false)) != 0) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_LOCATE_OLD_BINLOG_OR_RELAY_LOG_FILES);
    goto err;
  }

  for (;;) {
    if ((error = my_delete_allow_opened(linfo.log_file_name, MYF(0))) != 0) {
      if (my_errno() == ENOENT) {
        LogErr(WARNING_LEVEL, ER_BINLOG_CANT_DELETE_FILE, linfo.log_file_name);
        error = 0;
      } else {
        LogErr(ERROR_LEVEL, ER_BINLOG_CANT_DELETE_FILE, linfo.log_file_name);
        goto err;
      }
    }
    if (find_next_log(&linfo, false)) break;
  }

  close(LOG_CLOSE_INDEX | LOG_CLOSE_TO_BE_OPENED, false, false);
  if ((error = my_delete_allow_opened(index_file_name, MYF(0)))) {
    if (my_errno() == ENOENT) {
      LogErr(WARNING_LEVEL, ER_BINLOG_CANT_DELETE_FILE, linfo.log_file_name);
      error = false;
    } else {
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_DELETE_FILE, index_file_name);
      goto err;
    }
  }

  name = const_cast<char *>(save_name);  // restore old file-name

err:
  mysql_mutex_unlock(&LOCK_index);
  return error;
}

int MYSQL_BIN_LOG::truncate_logs_from_index(
    const char *last_file_name, std::vector<std::string> &delete_vector) {
  int error = 0;
  LOG_INFO log_info;
  bool found = false;
  DBUG_TRACE;

  mysql_mutex_assert_owner(&LOCK_index);

  if ((error = find_log_pos(&log_info, NullS, false))) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
    goto err;
  }

  if (open_crash_safe_index_file()) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_TMP_INDEX,
           "MYSQL_BIN_LOG::truncate_logs_from_index");
    goto err;
  }

  while (true) {
    if (!found) {
      if ((error = my_b_write(&crash_safe_index_file,
                              (const uchar *)log_info.log_file_name,
                              strlen(log_info.log_file_name))) ||
          (error =
               my_b_write(&crash_safe_index_file, (const uchar *)"\n", 1))) {
        LogErr(ERROR_LEVEL, ER_BINLOG_CANT_APPEND_LOG_TO_TMP_INDEX,
               log_info.log_file_name);
        break;
      }

      if (!compare_log_name(last_file_name, log_info.log_file_name))
        found = true;
    } else {
      delete_vector.push_back(std::string(log_info.log_file_name));
    }
    if (find_next_log(&log_info, false)) break;
  }

  if (close_crash_safe_index_file()) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_CLOSE_TMP_INDEX,
           "MYSQL_BIN_LOG::truncate_logs_from_index");
    goto err;
  }
  DBUG_EXECUTE_IF("fault_injection_copy_part_file", DBUG_SUICIDE(););

  if (move_crash_safe_index_file_to_index_file(false)) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_MOVE_TMP_TO_INDEX,
           "MYSQL_BIN_LOG::truncate_logs_from_index");
    goto err;
  }
  adjust_linfo_offsets(log_info.index_file_start_offset);
  return 0;

err:
  return -1;
}

int MYSQL_BIN_LOG::truncate_files_after(const char *file_name,
                                        bool need_lock_index) {
  int error = 0;
  std::vector<std::string> delete_vector;
  DBUG_TRACE;

  if (need_lock_index) mysql_mutex_lock(&LOCK_index);

  error = truncate_logs_from_index(file_name, delete_vector);

  // delete file
  for (std::vector<std::string>::iterator iter = delete_vector.begin();
       !error && iter != delete_vector.end(); iter++) {
    LogErr(SYSTEM_LEVEL, ER_CONSENSUS_LOG_TRUNCATE_FILES, "file",
           (*iter).c_str());
    if (mysql_file_delete(key_file_binlog, (*iter).c_str(), MYF(0))) {
      error = 1;
      break;
    }
  }

  if (need_lock_index) mysql_mutex_unlock(&LOCK_index);

  return error;
}

int MYSQL_BIN_LOG::truncate_log(const char *file_name, my_off_t offset,
                                Relay_log_info *rli) {
  int error = 0;
  const char *save_name = nullptr;

  LogErr(SYSTEM_LEVEL, ER_CONSENSUS_TRUNCATE_LOG, file_name, offset,
         is_consensus_write);

  mysql_mutex_assert_owner(&LOCK_log);

  if (rli) mysql_mutex_lock(&rli->data_lock);

  if (!is_active(file_name)) {
    LogErr(SYSTEM_LEVEL, ER_CONSENSUS_LOG_TRUNCATE_FILES, "files after",
           file_name);

    mysql_mutex_lock(&LOCK_index);

    /* Save variables so that we can reopen the log */
    save_name = name;
    name = nullptr;

    close(LOG_CLOSE_TO_BE_OPENED, false, false);

    if (truncate_files_after(file_name, false)) {
      LogErr(ERROR_LEVEL, ER_CONSENSUS_LOG_TRUNCATE_FILES_AFTER_ERROR,
             file_name);
      abort();
    }

    close(LOG_CLOSE_INDEX | LOG_CLOSE_TO_BE_OPENED, false, false);

    if (open_index_file(index_file_name, nullptr, false) ||
        open_exist_consensus_binlog(save_name, max_size, false, false)) {
      error = 1;
      goto err;
    }

    mysql_mutex_unlock(&LOCK_index);

    if (name != nullptr) {
      my_free(const_cast<char *>(save_name));
      save_name = nullptr;
    }
  }

  assert(is_active(file_name));
  if (is_active(file_name)) {
    mysql_mutex_lock(&LOCK_sync);
    if (m_binlog_file->truncate(offset)) {
      mysql_mutex_unlock(&LOCK_sync);
      LogErr(ERROR_LEVEL, ER_CONSENSUS_LOG_TRUNCATE_OFFSET_ERROR, file_name,
             offset);
      error = 1;
      goto err;
    }
    atomic_binlog_end_pos = offset;
    mysql_mutex_unlock(&LOCK_sync);
  }

err:
  if (rli) {
    mysql_mutex_unlock(&rli->data_lock);

    if (!error) rli->notify_relay_log_truncated();
  }

  if (name == nullptr)
    name = const_cast<char *>(save_name);  // restore old file-name

  return error;
}

int MYSQL_BIN_LOG::switch_and_seek_log(const char *file_name, my_off_t offset,
                                       bool need_lock_index) {
  int error = 0;
  myf flags = MY_WME | MY_NABP;

  mysql_mutex_assert_owner(&LOCK_log);

  if (!is_active(file_name)) {
    if (need_lock_index) mysql_mutex_lock(&LOCK_index);

    close(LOG_CLOSE_INDEX | LOG_CLOSE_TO_BE_OPENED, false, false);

    if (open_index_file(index_file_name, nullptr, false)) {
      if (need_lock_index) mysql_mutex_unlock(&LOCK_index);
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING, name, errno);
      error = 1;
      goto err;
    }

    if (init_and_set_log_file_name(name, file_name, 0)) {
      if (need_lock_index) mysql_mutex_unlock(&LOCK_index);
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_GENERATE_NEW_FILE_NAME);
      error = 1;
      goto err;
    }

    mysql_mutex_lock(&LOCK_sync);
    std::unique_ptr<Binlog_ofile> ofile(
        m_binlog_file->open_existing(m_key_file_log, log_file_name, flags));
    if (ofile == nullptr) {
      mysql_mutex_unlock(&LOCK_sync);
      if (need_lock_index) mysql_mutex_unlock(&LOCK_index);
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING, name, errno);
      error = 1;
      goto err;
    }
    m_binlog_file = ofile.release();
    mysql_mutex_unlock(&LOCK_sync);

    atomic_log_state = LOG_OPENED;

    if (need_lock_index) mysql_mutex_unlock(&LOCK_index);
  }

  m_binlog_file->seek(offset);
  update_binlog_end_pos();

  return 0;

err:
  if (name) {
    my_free(name);
    name = nullptr;
  }
  atomic_log_state = LOG_CLOSED;
  return error;
}

int MYSQL_BIN_LOG::reopen_log_index(bool need_lock_index) {
  int error = 0;
  File fd = -1;
  DBUG_TRACE;

  if (need_lock_index)
    mysql_mutex_lock(&LOCK_index);
  else
    mysql_mutex_assert_owner(&LOCK_index);

  if (my_b_inited(&index_file)) {
    end_io_cache(&index_file);
    mysql_file_close(index_file.file, MYF(0));
  }

  if ((fd = mysql_file_open(key_file_binlog_index, index_file_name, O_RDWR,
                            MYF(MY_WME))) < 0 ||
      init_io_cache_ext(&index_file, fd, IO_SIZE, READ_CACHE,
                        mysql_file_seek(fd, 0L, MY_SEEK_END, MYF(0)), false,
                        MYF(MY_WME | MY_WAIT_IF_FULL),
                        key_file_binlog_index_cache)) {
    LogErr(ERROR_LEVEL, ER_BINLOG_FAILED_TO_OPEN_INDEX_FILE_AFTER_REBUILDING,
           index_file_name);
    goto fatal_err;
  }

  if (need_lock_index) mysql_mutex_unlock(&LOCK_index);
  return error;

fatal_err:
  /*
    This situation is very very rare to happen (unless there is some serious
    memory related issues like OOM) and should be treated as fatal error.
    Hence it is better to bring down the server without respecting
    'binlog_error_action' value here.
  */
  exec_binlog_error_action_abort(
      "MySQL server failed to update the "
      "binlog.index file's content properly. "
      "It might not be in sync with available "
      "binlogs and the binlog.index file state is in "
      "unrecoverable state. Aborting the server.");
  /*
    Server is aborted in the above function.
    This is dead code to make compiler happy.
   */
  return error;
}

/**
Open a already existed binlog file for consensus replication

- Open the log file and the index file.
- When calling this when the file is in use, you must have a locks
on LOCK_log and LOCK_index.

@retval
0	ok
@retval
1	error
*/

bool MYSQL_BIN_LOG::open_exist_consensus_binlog(const char *log_name,
                                                ulong max_size_arg,
                                                bool set_flags,
                                                bool need_lock_index) {
  LOG_INFO log_info;
  int error = 1;
  DBUG_TRACE;

  DBUG_PRINT("enter", ("base filename: %s", log_name));

  mysql_mutex_assert_owner(get_log_lock());

  // find last log name according to the binlog index
  if (!my_b_inited(&index_file)) {
    cleanup();
    return true;
  }
  if ((error = find_log_pos(&log_info, NullS, need_lock_index))) {
    if (error != LOG_INFO_EOF) {
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
      return true;
    }
  }

  if (error == 0) {
    do {
      strmake(log_file_name, log_info.log_file_name, sizeof(log_file_name) - 1);
    } while (!(error = find_next_log(&log_info, need_lock_index)));
  }

  if (error != LOG_INFO_EOF) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_FIND_LOG_IN_INDEX, error);
    return true;
  }

  // #ifdef HAVE_REPLICATION
  DBUG_EXECUTE_IF("crash_create_non_critical_before_update_index",
                  DBUG_SUICIDE(););
  // #endif

  write_error = false;

  /* open the main log file */
  if (open_for_consensus(
#ifdef HAVE_PSI_INTERFACE
          m_key_file_log,
#endif
          log_name, set_flags)) {
    return true;
  }

  max_size = max_size_arg;

  atomic_log_state = LOG_OPENED;

  /*
    At every rotate memorize the last transaction counter state to use it as
    offset at logging the transaction logical timestamps.
  */
  m_dependency_tracker.rotate();

  if (is_consensus_write) update_binlog_end_pos();
  return false;
}

static enum_read_gtids_from_binlog_status read_gtids_from_consensus_binlog(
    const char *filename, Gtid_set *all_gtids, Gtid_set *prev_gtids,
    Gtid *first_gtid, Sid_map *sid_map, bool verify_checksum,
    uint64 last_index) {
  DBUG_TRACE;
  DBUG_PRINT("info", ("Opening file %s", filename));

#ifndef NDEBUG
  unsigned long event_counter = 0;
  /*
    We assert here that both all_gtids and prev_gtids, if specified,
    uses the same sid_map as the one passed as a parameter. This is just
    to ensure that, if the sid_map needed some lock and was locked by
    the caller, the lock applies to all the GTID sets this function is
    dealing with.
  */
  if (all_gtids) assert(all_gtids->get_sid_map() == sid_map);
  if (prev_gtids) assert(prev_gtids->get_sid_map() == sid_map);
#endif

  Binlog_file_reader binlog_file_reader(verify_checksum);
  if (binlog_file_reader.open(filename)) {
    LogErr(ERROR_LEVEL, ER_BINLOG_FILE_OPEN_FAILED,
           binlog_file_reader.get_error_str());
    /*
      We need to revisit the recovery procedure for relay log
      files. Currently, it is called after this routine.
      /Alfranio
    */
    if (binlog_file_reader.get_error_type() ==
        Binlog_read_error::CANNOT_GET_FILE_PASSWORD)
      return ERROR;
    return TRUNCATED;
  }

  Log_event *ev = nullptr;
  enum_read_gtids_from_binlog_status ret = NO_GTIDS;
  bool done = false;
  bool seen_first_gtid = false;
  while (!done && (ev = binlog_file_reader.read_event_object()) != nullptr) {
#ifndef NDEBUG
    event_counter++;
#endif
    DBUG_PRINT("info", ("Read event of type %s", ev->get_type_str()));
    switch (ev->get_type_code()) {
      case binary_log::FORMAT_DESCRIPTION_EVENT:
      case binary_log::ROTATE_EVENT:
      case binary_log::PREVIOUS_CONSENSUS_INDEX_LOG_EVENT:
        // do nothing; just accept this event and go to next
        break;
      case binary_log::PREVIOUS_GTIDS_LOG_EVENT: {
        ret = GOT_PREVIOUS_GTIDS;
        // add events to sets
        Previous_gtids_log_event *prev_gtids_ev =
            (Previous_gtids_log_event *)ev;
        if (all_gtids != nullptr && prev_gtids_ev->add_to_set(all_gtids) != 0)
          ret = ERROR, done = true;
        else if (prev_gtids != nullptr &&
                 prev_gtids_ev->add_to_set(prev_gtids) != 0)
          ret = ERROR, done = true;
#ifndef NDEBUG
        char *prev_buffer = prev_gtids_ev->get_str(nullptr, nullptr);
        DBUG_PRINT("info", ("Got Previous_gtids from file '%s': Gtid_set='%s'.",
                            filename, prev_buffer));
        my_free(prev_buffer);
#endif
        /* Only get previous gtid set */
        if (prev_gtids != nullptr && all_gtids == nullptr &&
            first_gtid == nullptr)
          done = true;
        break;
      }
      case binary_log::GTID_LOG_EVENT: {
        if (ret != GOT_GTIDS) {
          if (ret != GOT_PREVIOUS_GTIDS) {
            /*
              Since this routine is run on startup, there may not be a
              THD instance. Therefore, ER(X) cannot be used.
             */
            const char *msg_fmt =
                (current_thd != nullptr)
                    ? ER_THD(current_thd, ER_BINLOG_LOGICAL_CORRUPTION)
                    : ER_DEFAULT(ER_BINLOG_LOGICAL_CORRUPTION);
            my_printf_error(
                ER_BINLOG_LOGICAL_CORRUPTION, msg_fmt, MYF(0), filename,
                "The first global transaction identifier was read, but "
                "no other information regarding identifiers existing "
                "on the previous log files was found.");
            ret = ERROR, done = true;
            break;
          } else {
            ret = GOT_GTIDS;
          }
        }
        /*
          When this is a relaylog, we just check if the relay log contains at
          least one Gtid_log_event, so that we can distinguish the return values
          GOT_GTID and GOT_PREVIOUS_GTIDS. We don't need to read anything else
          from the relay log.
          When this is a binary log, if all_gtids is requested (i.e., NOT nullptr),
          we should continue to read all gtids. If just first_gtid was
          requested, we will be done after storing this Gtid_log_event info on
          it.
        */
        {
          Gtid_log_event *gtid_ev = (Gtid_log_event *)ev;
          rpl_sidno sidno = gtid_ev->get_sidno(sid_map);
          if (sidno < 0)
            ret = ERROR, done = true;
          else {
            if (all_gtids) {
              if (all_gtids->ensure_sidno(sidno) != RETURN_STATUS_OK)
                ret = ERROR, done = true;
              all_gtids->_add_gtid(sidno, gtid_ev->get_gno());
              DBUG_PRINT("info",
                         ("Got Gtid from file '%s': Gtid(%d, %" PRId64 ").",
                          filename, sidno, gtid_ev->get_gno()));
            }

            /* If the first GTID was requested, stores it */
            if (first_gtid && !seen_first_gtid) {
              first_gtid->set(sidno, gtid_ev->get_gno());
              seen_first_gtid = true;
              /* If the first_gtid was the only thing requested, we are done */
              if (all_gtids == nullptr) ret = GOT_GTIDS, done = true;
            }
          }
        }
        break;
      }
      case binary_log::CONSENSUS_LOG_EVENT: {
        if (last_index != 0) {
          Consensus_log_event *r_ev = (Consensus_log_event *)ev;
          uint64 consensus_index = r_ev->get_index();
          if (consensus_index >= last_index) {
            done = true;
          }
        }
        break;
      }
      case binary_log::ANONYMOUS_GTID_LOG_EVENT: {
        assert(prev_gtids == nullptr
                   ? true
                   : all_gtids != nullptr || first_gtid != nullptr);
      }
        [[fallthrough]];
      default:
        // if we found any other event type without finding a
        // previous_gtids_log_event, then the rest of this binlog
        // cannot contain gtids
        if (ret != GOT_GTIDS && ret != GOT_PREVIOUS_GTIDS) done = true;
        break;
    }
    delete ev;
    DBUG_PRINT("info", ("done=%d", done));
  }

  if (binlog_file_reader.has_fatal_error()) {
    // This is not a fatal error; the log may just be truncated.

    // @todo but what other errors could happen? IO error?
    LogErr(WARNING_LEVEL, ER_BINLOG_ERROR_READING_GTIDS_FROM_BINARY_LOG, -1);
  }

  if (all_gtids)
    all_gtids->dbug_print("all_gtids");
  else
    DBUG_PRINT("info", ("all_gtids==NULL"));
  if (prev_gtids)
    prev_gtids->dbug_print("prev_gtids");
  else
    DBUG_PRINT("info", ("prev_gtids==NULL"));
  if (first_gtid == nullptr)
    DBUG_PRINT("info", ("first_gtid==NULL"));
  else if (first_gtid->sidno == 0)
    DBUG_PRINT("info", ("first_gtid.sidno==0"));
  else
    first_gtid->dbug_print(sid_map, "first_gtid");

  DBUG_PRINT("info", ("returning %d", ret));
#ifndef NDEBUG
  if (prev_gtids != nullptr && all_gtids == nullptr && first_gtid == nullptr)
    LogErr(INFORMATION_LEVEL, ER_BINLOG_EVENTS_READ_FROM_BINLOG_INFO,
           event_counter, filename);
#endif
  return ret;
}

bool MYSQL_BIN_LOG::consensus_init_gtid_sets(
    Gtid_set *all_gtids, Gtid_set *lost_gtids, bool verify_checksum,
    bool need_lock, bool is_server_starting, uint64 last_index,
    const char *log_name) {
  DBUG_TRACE;
  DBUG_PRINT("info", ("lost_gtids=%p; so we are recovering a %s log; ",
                      lost_gtids, lost_gtids == nullptr ? "relay" : "binary"));

  Checkable_rwlock *sid_lock = (is_consensus_write && all_gtids)
                                   ? all_gtids->get_sid_map()->get_sid_lock()
                                   : global_sid_lock;

  /*
    Acquires the necessary locks to ensure that logs are not either
    removed or updated when we are reading from it.
  */
  if (need_lock) {
    // We don't need LOCK_log if we are only going to read the initial
    // Prevoius_gtids_log_event and ignore the Gtid_log_events.
    if (all_gtids != nullptr) mysql_mutex_lock(&LOCK_log);
    mysql_mutex_lock(&LOCK_index);
    sid_lock->wrlock();
  } else {
    if (all_gtids != nullptr) mysql_mutex_assert_owner(&LOCK_log);
    mysql_mutex_assert_owner(&LOCK_index);
    sid_lock->assert_some_wrlock();
  }

  /* Initialize the sid_map to be used in read_gtids_from_consensus_binlog */
  Sid_map *sid_map = nullptr;
  if (all_gtids)
    sid_map = all_gtids->get_sid_map();
  else if (lost_gtids)
    sid_map = lost_gtids->get_sid_map();

  // Gather the set of files to be accessed.
  auto log_index = this->get_log_index(false);
  std::list<std::string> filename_list = log_index.second;
  int error = log_index.first;
  list<string>::iterator it;
  list<string>::reverse_iterator rit;
  bool reached_first_file = false;
  bool can_stop_reading = false;

  if (error != LOG_INFO_EOF) {
    DBUG_PRINT("error", ("Error reading binlog index"));
    goto end;
  }

  /*
    On server starting, one new empty binlog file is created and
    its file name is put into index file before initializing
    GLOBAL.GTID_EXECUTED AND GLOBAL.GTID_PURGED, it is not the
    last binlog file before the server restarts, so we remove
    its file name from filename_list.
  */
  if (is_server_starting && !is_consensus_write && !filename_list.empty())
    filename_list.pop_back();

  if (log_name != nullptr) {
    auto target = std::find(filename_list.begin(), filename_list.end(),
                            std::string(log_name));
    if (target != filename_list.end())
      filename_list.erase(std::next(target), filename_list.end());
  }

  error = 0;
  DBUG_PRINT("info", ("Iterating backwards through binary logs, "
                      "looking for the last binary log that contains "
                      "a Previous_gtids_log_event."));
  // Iterate over all files in reverse order until we find one that
  // contains a Previous_gtids_log_event.
  rit = filename_list.rbegin();
  reached_first_file = (rit == filename_list.rend());
  DBUG_PRINT("info",
             ("filename='%s' reached_first_file=%d",
              reached_first_file ? "" : rit->c_str(), reached_first_file));
  while (!can_stop_reading && !reached_first_file) {
    const char *filename = rit->c_str();
    assert(rit != filename_list.rend());
    rit++;
    reached_first_file = (rit == filename_list.rend());
    DBUG_PRINT("info", ("filename='%s' can_stop_reading=%d "
                        "reached_first_file=%d, ",
                        filename, can_stop_reading, reached_first_file));
    switch (read_gtids_from_consensus_binlog(
        filename, all_gtids, reached_first_file ? lost_gtids : nullptr,
        nullptr /* first_gtid */, sid_map, verify_checksum, last_index)) {
      case ERROR: {
        error = 1;
        goto end;
      }
      case GOT_GTIDS:
      case GOT_PREVIOUS_GTIDS: {
        can_stop_reading = true;
        break;
      }
      case NO_GTIDS:
      case TRUNCATED: {
        break;
      }
    }
  }
  if (lost_gtids != nullptr && !reached_first_file) {
    DBUG_PRINT("info", ("Iterating forwards through binary logs, looking for "
                        "the first binary log that contains both a "
                        "Previous_gtids_log_event and a Gtid_log_event."));
    for (it = filename_list.begin(); it != filename_list.end(); it++) {
      /*
        We should pass a first_gtid to read_gtids_from_consensus_binlog when
        binlog_gtid_simple_recovery is disabled, or else it will return
        right after reading the PREVIOUS_GTIDS event to avoid stall on
        reading the whole binary log.
      */
      Gtid first_gtid = {0, 0};
      const char *filename = it->c_str();
      DBUG_PRINT("info", ("filename='%s'", filename));
      switch (read_gtids_from_consensus_binlog(
          filename, nullptr, lost_gtids,
          binlog_gtid_simple_recovery ? nullptr : &first_gtid, sid_map,
          verify_checksum, 0)) {
        case ERROR: {
          error = 1;
          [[fallthrough]];
        }
        case GOT_GTIDS: {
          goto end;
        }
        case NO_GTIDS:
        case GOT_PREVIOUS_GTIDS: {
          /*
            Mysql server iterates forwards through binary logs, looking for
            the first binary log that contains both Previous_gtids_log_event
            and gtid_log_event for gathering the set of gtid_purged on server
            start. It also iterates forwards through binary logs, looking for
            the first binary log that contains both Previous_gtids_log_event
            and gtid_log_event for gathering the set of gtid_purged when
            purging binary logs. This may take very long time if it has many
            binary logs and almost all of them are out of filesystem cache.
            So if the binlog_gtid_simple_recovery is enabled, we just
            initialize GLOBAL.GTID_PURGED from the first binary log, do not
            read any more binary logs.
          */
          if (binlog_gtid_simple_recovery) goto end;
          [[fallthrough]];
        }
        case TRUNCATED: {
          break;
        }
      }
    }
  }
end:
  if (all_gtids) all_gtids->dbug_print("all_gtids");
  if (lost_gtids) lost_gtids->dbug_print("lost_gtids");
  if (need_lock) {
    sid_lock->unlock();
    mysql_mutex_unlock(&LOCK_index);
    if (all_gtids != nullptr) mysql_mutex_unlock(&LOCK_log);
  }
  filename_list.clear();
  DBUG_PRINT("info", ("returning %d", error));
  return error != 0 ? true : false;
}

/**
  Open a (new) binlog file.

  - Open the log file and the index file. Register the new
  file name in it
  - When calling this when the file is in use, you must have a locks
  on LOCK_log and LOCK_index.

  @retval
    0	ok
  @retval
    1	error
*/

int MYSQL_BIN_LOG::init_consensus_binlog(bool null_created_arg,
                                         bool need_sid_lock,
                                         bool &write_file_name_to_index_file,
                                         uint64 current_index, uint32 &tv_sec) {
  DBUG_TRACE;

  /* This must be before goto err. */
  Format_description_log_event s;
  Previous_consensus_index_log_event prev_consensus_index_ev(current_index);

  if (m_binlog_file->is_empty()) {
    /*
      The binary log file was empty (probably newly created)
      This is the normal case and happens when the user doesn't specify
      an extension for the binary log files.
      In this case we write a standard header to it.
    */
    if (m_binlog_file->write(pointer_cast<const uchar *>(BINLOG_MAGIC),
                             BIN_LOG_HEADER_SIZE))
      return 1;
    bytes_written += BIN_LOG_HEADER_SIZE;
    write_file_name_to_index_file = true;
  }

  if (!is_consensus_write)
    s.common_header->flags |= LOG_EVENT_BINLOG_IN_USE_F;
  else if (tv_sec > 0)
    s.common_header->when.tv_sec = tv_sec;

  if (!s.is_valid()) goto err;
  s.dont_set_created = null_created_arg;
  /* Set LOG_EVENT_RELAY_LOG_F flag for relay log's FD */
  if (write_event_to_binlog(&s)) goto err;

  if (is_consensus_write && tv_sec > 0)
    prev_consensus_index_ev.common_header->when.tv_sec = tv_sec;

  // write previous consensus index event
  if (write_event_to_binlog(&prev_consensus_index_ev)) goto err;

  if (!is_consensus_write)
    tv_sec = prev_consensus_index_ev.common_header->when.tv_sec;

  /*
    We need to revisit this code and improve it.
    See further comments in the mysqld.
    /Alfranio
  */
  if (current_thd) {
    Checkable_rwlock *sid_lock = nullptr;
    Gtid_set logged_gtids_binlog(global_sid_map, global_sid_lock);
    Gtid_set *previous_logged_gtids;

    if (is_consensus_write) {
      previous_logged_gtids = previous_gtid_set_relaylog;
      sid_lock = previous_gtid_set_relaylog->get_sid_map()->get_sid_lock();
    } else {
      previous_logged_gtids = &logged_gtids_binlog;
      sid_lock = global_sid_lock;
    }

    if (need_sid_lock)
      sid_lock->wrlock();
    else
      sid_lock->assert_some_wrlock();

    if (!is_consensus_write) {
      const Gtid_set *executed_gtids = gtid_state->get_executed_gtids();
      const Gtid_set *gtids_only_in_table =
          gtid_state->get_gtids_only_in_table();
      /* logged_gtids_binlog= executed_gtids - gtids_only_in_table */
      if (logged_gtids_binlog.add_gtid_set(executed_gtids) !=
          RETURN_STATUS_OK) {
        if (need_sid_lock) sid_lock->unlock();
        goto err;
      }
      logged_gtids_binlog.remove_gtid_set(gtids_only_in_table);
    }
    DBUG_PRINT("info", ("Generating PREVIOUS_GTIDS for binlog file."));
    Previous_gtids_log_event prev_gtids_ev(previous_logged_gtids);
    if (is_consensus_write && tv_sec > 0)
      prev_gtids_ev.common_header->when.tv_sec = tv_sec;
    if (need_sid_lock) sid_lock->unlock();
    if (write_event_to_binlog(&prev_gtids_ev)) goto err;
  } else  // !(current_thd)
  {
    /*
      If the slave was configured before server restart, the server will
      generate a new relay log file without having current_thd, but this
      new relay log file must have a PREVIOUS_GTIDS event as we now
      generate the PREVIOUS_GTIDS event always.

      This is only needed for relay log files because the server will add
      the PREVIOUS_GTIDS of binary logs (when current_thd==NULL) after
      server's GTID initialization.

      During server's startup at mysqld_main(), from the binary/relay log
      initialization point of view, it will:
      1) Call init_server_components() that will generate a new binary log
         file but won't write the PREVIOUS_GTIDS event yet;
      2) Initialize server's GTIDs;
      3) Write the binary log PREVIOUS_GTIDS;
      4) Call init_replica() in where the new relay log file will be created
         after initializing relay log's Retrieved_Gtid_Set;
    */
    if (is_consensus_write) {
      Sid_map *previous_gtid_sid_map =
          previous_gtid_set_relaylog->get_sid_map();
      Checkable_rwlock *sid_lock = previous_gtid_sid_map->get_sid_lock();

      if (need_sid_lock)
        sid_lock->wrlock();
      else
        sid_lock->assert_some_wrlock(); /* purecov: inspected */

      DBUG_PRINT("info", ("Generating PREVIOUS_GTIDS for relaylog file."));
      Previous_gtids_log_event prev_gtids_ev(previous_gtid_set_relaylog);
      prev_gtids_ev.set_relay_log_event();
      if (tv_sec > 0) prev_gtids_ev.common_header->when.tv_sec = tv_sec;

      if (need_sid_lock) sid_lock->unlock();
      if (write_event_to_binlog(&prev_gtids_ev)) goto err;
    }
  }
  if (m_binlog_file->flush_and_sync()) goto err;

  return 0;
err:
  return 1;
}

/**
  Generate to a new log file from archive logs.

  @param need_lock_log If true, this function acquires LOCK_log;
  otherwise the caller should already have acquired it.

  @retval 0 success
  @retval nonzero - error

  @note The new file name is stored last in the index file
*/
int MYSQL_BIN_LOG::new_file_from_archive(const char *archive_log_name,
                                         bool need_lock_log) {
  int error = 0;
  bool close_on_error = false;
  char new_name[FN_REFLEN], *new_name_ptr = nullptr, *old_name, *file_to_open;
  const size_t ERR_CLOSE_MSG_LEN = 1024;
  char close_on_error_msg[ERR_CLOSE_MSG_LEN];
  memset(close_on_error_msg, 0, sizeof close_on_error_msg);

  DBUG_TRACE;
  if (!is_open()) {
    DBUG_PRINT("info", ("log is closed"));
    return error;
  }

  if (need_lock_log)
    mysql_mutex_lock(&LOCK_log);
  else
    mysql_mutex_assert_owner(&LOCK_log);

  mysql_mutex_lock(&LOCK_index);

  mysql_mutex_assert_owner(&LOCK_log);
  mysql_mutex_assert_owner(&LOCK_index);

  /*
    If user hasn't specified an extension, generate a new log name
    We have to do this here and not in open as we want to store the
    new file name in the current binary log file.
  */
  new_name_ptr = new_name;
  if ((error = generate_new_name(new_name, name))) {
    // Use the old name if generation of new name fails.
    strcpy(new_name, name);
    close_on_error = true;
    snprintf(close_on_error_msg, sizeof close_on_error_msg,
             ER_THD(current_thd, ER_NO_UNIQUE_LOGFILE), name);
    if (strlen(close_on_error_msg)) {
      close_on_error_msg[strlen(close_on_error_msg) - 1] = '\0';
    }
    goto end;
  }

  /*
    Make sure that the log_file is initialized before writing
    Rotate_log_event into it.
  */
  if (m_binlog_file->is_open()) {
    /*
      We log the whole file name for log file as the user may decide
      to change base names at some point.
    */
    Rotate_log_event r(new_name + dirname_length(new_name), 0, LOG_EVENT_OFFSET,
                       0);

    if (DBUG_EVALUATE_IF("fault_injection_new_file_from_archive_rotate_event",
                         (error = 1), false) ||
        (error = write_event_to_binlog(&r))) {
      char errbuf[MYSYS_STRERROR_SIZE];
      DBUG_EXECUTE_IF("fault_injection_new_file_from_archive_rotate_event",
                      errno = 2;);
      close_on_error = true;
      snprintf(close_on_error_msg, sizeof close_on_error_msg,
               ER_THD(current_thd, ER_ERROR_ON_WRITE), name, errno,
               my_strerror(errbuf, sizeof(errbuf), errno));
      my_printf_error(ER_ERROR_ON_WRITE, ER_THD(current_thd, ER_ERROR_ON_WRITE),
                      MYF(ME_FATALERROR), name, errno,
                      my_strerror(errbuf, sizeof(errbuf), errno));
      goto end;
    }

    if ((error = m_binlog_file->flush())) {
      close_on_error = true;
      snprintf(close_on_error_msg, sizeof close_on_error_msg, "%s",
               "Either disk is full or file system is read only");
      goto end;
    }
  }

  old_name = name;
  name = nullptr;  // Don't free name
  close(LOG_CLOSE_TO_BE_OPENED | LOG_CLOSE_INDEX, false /*need_lock_log=false*/,
        false /*need_lock_index=false*/);

  /* reopen index binlog file, BUG#34582 */
  file_to_open = index_file_name;
  error = open_index_file(index_file_name, nullptr,
                          false /*need_lock_index=false*/);
  if (!error) {
    /* generate and open the binary log file. */
    file_to_open = new_name_ptr;
    error = open_binlog_from_archive(
#ifdef HAVE_PSI_INTERFACE
        m_key_file_log,
#endif
        old_name, new_name_ptr, max_size, archive_log_name,
        false /*need_lock_index=false*/);
  }

  /* handle reopening errors */
  if (error) {
    char errbuf[MYSYS_STRERROR_SIZE];
    my_printf_error(ER_CANT_OPEN_FILE, ER_THD(current_thd, ER_CANT_OPEN_FILE),
                    MYF(ME_FATALERROR), file_to_open, error,
                    my_strerror(errbuf, sizeof(errbuf), error));
    close_on_error = true;
    snprintf(close_on_error_msg, sizeof close_on_error_msg,
             ER_THD(current_thd, ER_CANT_OPEN_FILE), file_to_open, error,
             my_strerror(errbuf, sizeof(errbuf), error));
  }
  my_free(old_name);

end:

  if (error && close_on_error /* rotate, flush or reopen failed */) {
    /*
      Close whatever was left opened.

      We are keeping the behavior as it exists today, ie,
      we disable logging and move on (see: BUG#51014).

      TODO: as part of WL#1790 consider other approaches:
       - kill mysql (safety);
       - try multiple locations for opening a log file;
       - switch server to protected/readonly mode
       - ...
    */
    if (binlog_error_action == ABORT_SERVER) {
      char abort_msg[ERR_CLOSE_MSG_LEN + 58];
      memset(abort_msg, 0, sizeof abort_msg);
      snprintf(abort_msg, sizeof abort_msg,
               "%s, while createthe binlog from archive. "
               "Aborting the server",
               close_on_error_msg);
      exec_binlog_error_action_abort(abort_msg);
    } else
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING,
             new_name_ptr != nullptr ? new_name_ptr : "new file", errno);

    close(LOG_CLOSE_INDEX, false /*need_lock_log=false*/,
          false /*need_lock_index=false*/);
  }

  mysql_mutex_unlock(&LOCK_index);
  if (need_lock_log) mysql_mutex_unlock(&LOCK_log);

  return error;
}

/**
  Copy a archived binlog file to new binlog file

  @retval 0	ok
  @retval 1	error
*/
static int copy_archive_log_file(const char *archive_file_name,
                                 const char *new_file_name) {
  int error = 0;
  uchar io_buf[IO_SIZE * 2];
  int bytes_read;
  IO_CACHE_ostream new_file_ostream;
  IO_CACHE_istream file_istream;
  myf flags = MY_WME | MY_NABP | MY_WAIT_IF_FULL;

  flags = flags | MY_REPORT_WAITING_IF_FULL;

  DBUG_TRACE;

  if (new_file_ostream.open(key_file_binlog, new_file_name, flags)) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING, new_file_name, errno);
    error = -1;
    goto err;
  }

  if (file_istream.open(key_file_binlog, key_file_binlog_cache,
                        archive_file_name, MYF(MY_WME | MY_DONT_CHECK_FILESIZE),
                        IO_SIZE * 2)) {
    new_file_ostream.close();
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_FOR_LOGGING, archive_file_name,
           errno);
    error = -1;
    goto err;
  }

  while (true) {
    if ((bytes_read = (int)file_istream.read(io_buf, sizeof(io_buf))) < 0) {
      error = -1;
      break;
    }

    if (DBUG_EVALUATE_IF("fault_injection_copy_part_file", 1, 0))
      bytes_read = bytes_read / 2;

    if (!bytes_read) break;  // end of file

    if (new_file_ostream.write(io_buf, bytes_read)) {
      error = -1;
      break;
    }

    if ((size_t)bytes_read < sizeof(io_buf)) break;  // end of file
  }

  if (new_file_ostream.flush() || new_file_ostream.sync()) {
    error = 1;
  }

  new_file_ostream.close();
  file_istream.close();

err:
  return error;
}

/**
  Open a (new) binlog file from archived binlog file.

  - Open the log file and the index file. Register the new
  file name in it
  - Copy archive binlog file to the new binlog file
  - When calling this when the file is in use, you must have a locks
  on LOCK_log and LOCK_index.

  @retval 0	ok
  @retval 1	error
*/

bool MYSQL_BIN_LOG::open_binlog_from_archive(
#ifdef HAVE_PSI_INTERFACE
    PSI_file_key log_file_key,
#endif
    const char *log_name, const char *new_name, ulong max_size_arg,
    const char *archive_log_name, bool need_lock_index) {
  DBUG_TRACE;
  DBUG_PRINT("enter", ("base filename: %s", log_name));

  uint32 new_index_number = 0;
  myf flags = MY_WME | MY_NABP | MY_WAIT_IF_FULL;
  std::unique_ptr<Binlog_ofile> ofile;
  std::string file_name;
  my_off_t file_off = 0;
  uchar in_use_flags = 0;
  MY_STAT info;

  flags = flags | MY_REPORT_WAITING_IF_FULL;

  mysql_mutex_assert_owner(get_log_lock());

  if (init_and_set_log_file_name(log_name, new_name, new_index_number)) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_GENERATE_NEW_FILE_NAME);
    return true;
  }

  DBUG_PRINT("info", ("generated filename: %s", log_file_name));

  if (open_purge_index_file(true) ||
      register_create_index_entry(log_file_name) || sync_purge_index_file() ||
      DBUG_EVALUATE_IF("fault_injection_registering_index", 1, 0)) {
    /**
      @todo: although this was introduced to appease valgrind
      when injecting emulated faults using fault_injection_registering_index
      it may be good to consider what actually happens when
      open_purge_index_file succeeds but register or sync fails.

      Perhaps we might need the code below in MYSQL_BIN_LOG::cleanup
      for "real life" purposes as well?
    */
    DBUG_EXECUTE_IF("fault_injection_registering_index", {
      if (my_b_inited(&purge_index_file)) {
        end_io_cache(&purge_index_file);
        my_close(purge_index_file.file, MYF(0));
      }
    });

    LogErr(ERROR_LEVEL, ER_BINLOG_FAILED_TO_SYNC_INDEX_FILE_IN_OPEN);
    return true;
  }

  DBUG_EXECUTE_IF("crash_create_non_critical_before_update_index",
                  DBUG_SUICIDE(););

  write_error = false;

  if (!(name = my_strdup(key_memory_MYSQL_LOG_name, log_name, MYF(MY_WME)))) {
    goto err;
  }

  write_error = false;

  db[0] = 0;

  /* Keep the key for reopen */
  m_log_file_key = log_file_key;

  /* Copy archive file to relay log file */
  if (copy_archive_log_file(archive_log_name, log_file_name)) {
    goto err;
  }

  ofile = Binlog_ofile::open_existing(
#ifdef HAVE_PSI_INTERFACE
      m_key_file_log,
#endif
      log_file_name, flags);

  if (ofile == nullptr) {
    goto err;
  }
  delete m_binlog_file;

  /* Clear LOG_EVENT_BINLOG_IN_USE_F */
  in_use_flags = 0;
  if (ofile->update(&in_use_flags, 1, BIN_LOG_HEADER_SIZE + FLAGS_OFFSET)) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_CLEAR_IN_USE_FLAG_FOR_CRASHED_BINLOG);
    goto err;
  }

  m_binlog_file = ofile.release();

  // write from the end
  if (!mysql_file_stat(log_file_key, log_file_name, &info, MYF(0))) {
    assert(!(my_errno() == ENOENT));
    goto err;
  }
  file_off = !m_binlog_file->is_encrypted()
                 ? info.st_size
                 : info.st_size - m_binlog_file->get_encrypted_header_size();
  m_binlog_file->truncate(file_off);

  max_size = max_size_arg;

  DBUG_EXECUTE_IF("crash_create_critical_before_update_index", DBUG_SUICIDE(););
  assert(my_b_inited(&index_file) != 0);

  /*
    The new log file name is appended into crash safe index file after
    all the content of index file is copied into the crash safe index
    file. Then move the crash safe index file to index file.
  */
  DBUG_EXECUTE_IF("simulate_disk_full_on_open_binlog",
                  { DBUG_SET("+d,simulate_no_free_space_error"); });
  if (DBUG_EVALUATE_IF("fault_injection_updating_index", 1, 0) ||
      add_log_to_index((uchar *)log_file_name, strlen(log_file_name),
                       need_lock_index)) {
    DBUG_EXECUTE_IF("simulate_disk_full_on_open_binlog", {
      DBUG_SET("-d,simulate_file_write_error");
      DBUG_SET("-d,simulate_no_free_space_error");
      DBUG_SET("-d,simulate_disk_full_on_open_binlog");
    });
    goto err;
  }

  DBUG_EXECUTE_IF("crash_create_after_update_index", DBUG_SUICIDE(););

  atomic_log_state = LOG_OPENED;

  /*
    At every rotate memorize the last transaction counter state to use it as
    offset at logging the transaction logical timestamps.
  */
  m_dependency_tracker.rotate();

  close_purge_index_file();

  update_binlog_end_pos();

  return false;

err:
  if (is_inited_purge_index_file())
    purge_index_entry(nullptr, nullptr, need_lock_index);
  close_purge_index_file();

  if (binlog_error_action == ABORT_SERVER) {
    exec_binlog_error_action_abort(
        "Either disk is full, file system is read only or "
        "there was an encryption error while opening the binlog. "
        "Aborting the server.");
  } else {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_USE_FOR_LOGGING,
           (new_name) ? new_name : name, errno);
    close(LOG_CLOSE_INDEX, false, need_lock_index);
  }

  if (name) {
    my_free(name);
    name = nullptr;
  }

  atomic_log_state = LOG_CLOSED;

  return true;
}

void binlog_update_end_pos(MYSQL_BIN_LOG *binlog, const char *file,
                           my_off_t pos) {
  binlog->update_binlog_end_pos(file, pos);
}

bool binlog_write_event_directly(MYSQL_BIN_LOG *binlog, Log_event *ev) {
  return binlog->write_event_to_binlog(ev);
}

my_off_t binlog_file_get_current_pos(MYSQL_BIN_LOG::Binlog_ofile *binlog_file) {
  return binlog_file->position();
}

int write_buffer_to_binlog_file(MYSQL_BIN_LOG::Binlog_ofile *binlog_file,
                                const unsigned char *buffer, my_off_t length) {
  return binlog_file->write(buffer, length);
}

my_off_t binlog_file_get_real_size(MYSQL_BIN_LOG::Binlog_ofile *binlog_file) {
  return binlog_file->get_real_file_size();
}

int binlog_file_flush_and_sync(MYSQL_BIN_LOG::Binlog_ofile *binlog_file) {
  if (binlog_file->flush()) return 1;
  if (binlog_file->sync()) return 1;

  return 0;
}

Binlog_cache_storage *binlog_cache_get_storage(binlog_cache_data *cache_data) {
  return cache_data->get_cache();
}

size_t binlog_cache_get_event_counter(binlog_cache_data *cache_data) {
  return cache_data->get_event_counter();
}

void update_trx_compression(binlog_cache_data *cache_data, Gtid &owned_gtid, 
                            uint64_t immediate_commit_timestamp) {
  binlog::global_context.monitoring_context()
      .transaction_compression()
      .update(binlog::monitoring::log_type::BINARY,
              cache_data->get_compression_type(), owned_gtid,
              immediate_commit_timestamp,
              cache_data->get_compressed_size(),
              cache_data->get_decompressed_size());
}

int truncate_binlog_file_to_valid_pos(const char *log_name, my_off_t valid_pos,
                                      my_off_t binlog_size, bool update) {
  std::unique_ptr<MYSQL_BIN_LOG::Binlog_ofile> ofile(
      MYSQL_BIN_LOG::Binlog_ofile::open_existing(key_file_binlog, log_name,
                                                 MYF(MY_WME)));

  if (!ofile) {
    LogErr(ERROR_LEVEL, ER_BINLOG_CANT_OPEN_CRASHED_BINLOG);
    return 1;
  }

  /* Change binlog file size to valid_pos */
  if (binlog_size > valid_pos) {
    if (ofile->truncate(valid_pos)) {
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_TRIM_CRASHED_BINLOG);
      return 1;
    }
    LogErr(SYSTEM_LEVEL, ER_BINLOG_CRASHED_BINLOG_TRIMMED, log_name,
           binlog_size, valid_pos, valid_pos);
  }

  if (update) {
    /* Clear LOG_EVENT_BINLOG_IN_USE_F */
    uchar flags = 0;
    if (ofile->update(&flags, 1, BIN_LOG_HEADER_SIZE + FLAGS_OFFSET)) {
      LogErr(ERROR_LEVEL, ER_BINLOG_CANT_CLEAR_IN_USE_FLAG_FOR_CRASHED_BINLOG);
      return 1;
    }
  }

  return 0;
}
