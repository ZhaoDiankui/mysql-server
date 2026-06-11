/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited

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

#ifndef CONSENSUS_PLUGIN_VARIABLES_INCLUDE
#define CONSENSUS_PLUGIN_VARIABLES_INCLUDE

#include <atomic>
#include <map>

#include "mysql/components/service.h"
#include "sql/server_component/mysql_transaction_delegate_control_imp.h"

/*
  Variables that have file context on plugin.cc

  All variables declared on this structure must be initialized
  on init() function.
*/
struct plugin_consensus_local_variables {
  MYSQL_PLUGIN plugin_info_ptr;
  unsigned int plugin_version;

  Checkable_rwlock *plugin_running_lock;
  Checkable_rwlock *plugin_stop_lock;
  std::atomic<bool> consensus_replication_running;

  SERVICE_TYPE(registry) * reg_srv;
  SERVICE_TYPE_NO_CONST(mysql_before_commit_transaction_control) *
      svc_mysql_before_commit_transaction_control;
  SERVICE_TYPE_NO_CONST(
      mysql_close_connection_of_binloggable_transaction_not_reached_commit) *
      svc_mysql_close_connection_of_binloggable_transaction_not_reached_commit;

  /*
    Initialize all variables, except mutexes.
  */
  void init() {
    plugin_info_ptr = nullptr;
    plugin_version = 0;

    plugin_running_lock = nullptr;
    plugin_stop_lock = nullptr;
    consensus_replication_running = false;

    reg_srv = nullptr;
    svc_mysql_before_commit_transaction_control = nullptr;
    svc_mysql_close_connection_of_binloggable_transaction_not_reached_commit = nullptr;
  }
};

#endif /* CONSENSUS_PLUGIN_VARIABLES_INCLUDE */
