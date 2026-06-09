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

#ifndef RPL_CONSENSUS_REPLICATION_INCLUDED
#define RPL_CONSENSUS_REPLICATION_INCLUDED

#include <string>

class THD;
class MYSQL_BIN_LOG;
class Trans_context_info;

typedef struct Binlog_context_info {
  MYSQL_BIN_LOG *binlog;
} Binlog_context_info;

typedef struct Consensus_binlog_context_info {
  uint64 consensus_index;
  uint64 consensus_term;
  bool binlog_disabled;
  bool consensus_replication_applier;
  bool status_locked;
  bool commit_locked;

  enum Consensus_error {
    CSS_NONE = 0,
    CSS_LEADERSHIP_CHANGE,
    CSS_LOG_TOO_LARGE,
    CSS_SHUTDOWN,
    CSS_GU_ERROR,
    CSS_OTHER
  } consensus_error;
} Consensus_binlog_context_info;

bool is_consensus_replication_plugin_loaded();
bool is_consensus_replication_enabled();
bool is_consensus_replication_applier_running();
bool is_consensus_replication_log_mode();
bool is_consensus_replication_state_leader(uint64 &term);

bool consensus_replication_show_logs(THD *thd);
bool consensus_replication_show_log_events(THD *thd);

void init_consensus_context(THD *thd);
void reset_consensus_context(THD *thd);

void cr_get_server_startup_prerequirements(Trans_context_info &requirements,
                                           Binlog_context_info &infos);

#endif /* RPL_CONSENSUS_REPLICATION_INCLUDED */
