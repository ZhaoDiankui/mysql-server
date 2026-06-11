/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Portions Copyright (c) 2015-Present, Facebook, Inc.
   Portions Copyright (c) 2012, Monty Program Ab

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
#pragma once

#include <string>
#include <mysql/psi/mysql_thread.h>
#include <mysql/psi/mysql_table.h>
#include <mysql/thread_pool_priv.h>
#include "util/se_utils.h"

namespace smartengine
{
class SeThread
{
private:
  // Disable Copying
  SeThread(const SeThread &);
  SeThread &operator=(const SeThread &);

  // Make sure we run only once
  std::atomic_bool m_run_once;

  my_thread_handle m_handle;

  std::string m_name;

protected:
  mysql_mutex_t m_signal_mutex;
  mysql_cond_t m_signal_cond;
  bool m_stop = false;

public:
  SeThread() : m_run_once(false) {}

#ifdef HAVE_PSI_INTERFACE
  void init(my_core::PSI_mutex_key stop_bg_psi_mutex_key,
            my_core::PSI_cond_key stop_bg_psi_cond_key);
  int create_thread(const std::string &thread_name,
                    my_core::PSI_thread_key background_psi_thread_key);
#else
  void init();
  int create_thread(const std::string &thread_name);
#endif

  virtual void run(void) = 0;

  void signal(const bool &stop_thread = false);

  int join() { return pthread_join(m_handle.thread, nullptr); }

  void setname()
  {
    /*
      mysql_thread_create() ends up doing some work underneath and setting the
      thread name as "my-func". This isn't what we want. Our intent is to name
      the threads according to their purpose so that when displayed under the
      debugger then they'll be more easily identifiable. Therefore we'll reset
      the name if thread was successfully created.
    */

    /*
      We originally had the creator also set the thread name, but that seems to
      not work correctly in all situations.  Having the created thread do the
      pthread_setname_np resolves the issue.
    */
    assert(!m_name.empty());
    int err = pthread_setname_np(m_handle.thread, m_name.c_str());
    if (err)
    {
      // NO_LINT_DEBUG
      sql_print_warning(
          "SE: Failed to set name (%s) for current thread, errno=%d",
          m_name.c_str(), errno);
    }
  }

  void uninit();

  virtual ~SeThread() {}

private:
  static void *thread_func(void *const thread_ptr);
};

/**
  MyX background thread control
  N.B. This is on top of se's own background threads
       (@see smartengine::common::CancelAllBackgroundWork())
*/

class SeBackgroundThread : public SeThread
{
private:
  bool m_save_stats = false;
  int32_t counter_ = 0;
  std::string backup_snapshot_file_;

  void reset() {
    mysql_mutex_assert_owner(&m_signal_mutex);
    m_stop = false;
    m_save_stats = false;
  }

public:
  virtual void run() override;

  void request_save_stats()
  {
    SE_MUTEX_LOCK_CHECK(m_signal_mutex);

    m_save_stats = true;

    SE_MUTEX_UNLOCK_CHECK(m_signal_mutex);
  }

  void set_snapshot_counter(int32_t counter)
  { 
    SE_MUTEX_LOCK_CHECK(m_signal_mutex);
    counter_ = counter; 
    SE_MUTEX_UNLOCK_CHECK(m_signal_mutex);
  }

  void set_backup_snapshot_hold_file(std::string &path)
  {
    backup_snapshot_file_ = path;
  }
};

/*
  Drop index thread control
*/

struct SeDropIndexThread : public SeThread
{
  virtual void run() override;
  // shrink extent space cann't do with the drop index
  bool is_run() { return m_run_; }
  void enter_race_condition() { SE_MUTEX_LOCK_CHECK(m_signal_mutex); }
  void exit_race_condition() { SE_MUTEX_UNLOCK_CHECK(m_signal_mutex); }

  static const int64_t purge_schedule_interval = 1800; // 30 minutes
private:
  bool m_run_;
};

/*
  thread for renewal single data node lease lock
*/
struct SeRenewLeaseLockThread : public SeThread
{
  virtual void run() override;
  bool is_run() { return m_run_; }

private:
  bool m_run_;
};

} //namespace smartengine
