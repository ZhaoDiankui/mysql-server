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

#include "consensus_recovery_manager.h"

#include "mysqld_error.h"
#include "my_loglevel.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/log.h"

#include "sql/log_event.h"

#define SECOND_PHASE_KEY "second_phase"

void ConsensusRecoveryManager::add_trx_to_binlog_map(my_xid xid,
                                                     uint64 current_index,
                                                     uint64 current_term) {
  DBUG_TRACE;
  if (this->recover_term == 0 || current_term > this->recover_term) {
    this->clear_all_map();
    this->recover_term = current_term;
  }

  DBUG_PRINT(
      "consensus_repl",
      ("Consensus recovery insert into transactions map(xid, consensus_index), "
       "transaction id: %llu, consensus index: %llu, state: %d",
       xid, (ulonglong)current_index,
       (int)enum_ha_recover_xa_state::COMMITTED));

  total_trx_index_map[xid] = current_index;
}

void ConsensusRecoveryManager::add_trx_to_binlog_xa_map(
    XID *xid, bool second_phase, uint64 current_index,
    uint64 current_term) {
  DBUG_TRACE;
  std::string xid_str(reinterpret_cast<char *>(xid->key()), xid->key_length());

  if (second_phase) xid_str.append(SECOND_PHASE_KEY, sizeof(SECOND_PHASE_KEY));

  if (this->recover_term == 0 || current_term > this->recover_term) {
    this->clear_all_map();
    this->recover_term = current_term;
  }

  DBUG_PRINT("consensus_repl",
             ("Consensus recovery insert into external transactions map(xid, "
              "consensus_index), transaction id: '%s', consensus index: %llu, "
              "state: %d",
              xid->get_data(), (ulonglong)current_index,
              second_phase ? (int)enum_ha_recover_xa_state::COMMITTED
                           : (int)enum_ha_recover_xa_state::PREPARED_IN_TC));

  total_xa_trx_index_map[xid_str] = current_index;
}

void ConsensusRecoveryManager::clear_all_map() {
  DBUG_TRACE;
  total_trx_index_map.clear();
  total_xa_trx_index_map.clear();
}

/* truncate commit_list and xa_list */
uint64 ConsensusRecoveryManager::reconstruct_binlog_trx_list(
    Xid_commit_list &commit_list, Xa_state_list &xa_list,
    Xa_state_list::list &se_xa_map, Xa_state_list::list &xa_map) {
  std::map<my_xid, uint64> trx_index_map = total_trx_index_map;
  std::map<std::string, uint64> xa_trx_index_map = total_xa_trx_index_map;

  DBUG_TRACE;

  // Erase uncommitted xid from trx_index_map and xa_trx_index_map
  for (auto iter = se_xa_map.begin(); iter != se_xa_map.end(); iter++) {
    my_xid xid = iter->first.get_my_xid();
    bool found = false;
    uint64 current_index = 0;

    if (!xid) {  // Externally coordinated transaction
      std::string xid_str(reinterpret_cast<const char *>(iter->first.key()),
                          iter->first.key_length());

      if (iter->second == enum_ha_recover_xa_state::PREPARED_IN_TC) {
        xid_str.append(SECOND_PHASE_KEY, sizeof(SECOND_PHASE_KEY));
      }

      auto iter_xa_trx_map = xa_trx_index_map.find(xid_str);
      if (iter_xa_trx_map != xa_trx_index_map.end()) {
        found = true;
        current_index = iter_xa_trx_map->second;
        xa_trx_index_map.erase(iter_xa_trx_map);
      }
      DBUG_PRINT(
          "consensus_repl",
          ("Consensus recovery found a transaction %s external transactions "
           "map, transaction id: '%s', consensus index: %llu, state: %d",
           found ? "in" : "not in", iter->first.get_data(),
           found ? (ulonglong)current_index : 0, (int)iter->second));
    } else {
      auto iter_trx_map = trx_index_map.find(xid);
      if (iter_trx_map != trx_index_map.end()) {
        found = true;
        current_index = iter_trx_map->second;
        trx_index_map.erase(iter_trx_map);
      }
      DBUG_PRINT("consensus_repl",
                 ("Consensus recovery found a prepared-in-se transaction %s "
                  "transactions map, transaction id: '%llu', consensus index: "
                  "%llu, state: %d",
                  found ? "in" : "not in", xid,
                  found ? (ulonglong)current_index : 0, (int)iter->second));
    }
  }

  // Find the max committed index from trx_index_map and xa_trx_index_map
  max_committed_index = 0;
  for (auto iter_trx_map = trx_index_map.begin();
       iter_trx_map != trx_index_map.end(); iter_trx_map++) {
    if (iter_trx_map->second > max_committed_index)
      max_committed_index = iter_trx_map->second;
  }
  for (auto iter_xa_trx_map = xa_trx_index_map.begin();
       iter_xa_trx_map != xa_trx_index_map.end(); iter_xa_trx_map++) {
    if (iter_xa_trx_map->second > max_committed_index)
      max_committed_index = iter_xa_trx_map->second;
  }

  // Erase trxs after max committed index from binlog xa_list(xa_map)
  for (auto iter = xa_map.begin(); iter != xa_map.end(); iter++) {
    std::string xid_str(reinterpret_cast<const char *>(iter->first.key()),
                        iter->first.key_length());

    enum_ha_recover_xa_state state = iter->second;
    if (state == enum_ha_recover_xa_state::COMMITTED ||
        state == enum_ha_recover_xa_state::ROLLEDBACK) {
      xid_str.append(SECOND_PHASE_KEY, sizeof(SECOND_PHASE_KEY));
    }

    auto iter_xa_trx_map = total_xa_trx_index_map.find(xid_str);
    if (iter_xa_trx_map != total_xa_trx_index_map.end() &&
        iter_xa_trx_map->second > max_committed_index) {
      LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_RECOVERY_EXTERN_TRX_LOGS,
                   "erase a prepared external transaction from binlog external "
                   "transactions list",
                   iter->first.get_data(), iter_xa_trx_map->second,
                   enum_ha_recover_xa_state::PREPARED_IN_SE);
      xa_map.erase(iter);
    }
  }

  // Append trxs from storage engine to xa_list
  for (auto iter = se_xa_map.begin(); iter != se_xa_map.end(); iter++) {
    xa_list.add(iter->first, iter->second);
  }

  // Erase trxs after max committed index from commit_list
  for (auto iter = commit_list.begin(); iter != commit_list.end(); iter++) {
    my_xid xid = *iter;
    auto iter_trx_map = total_trx_index_map.find(xid);

    if (iter_trx_map != total_trx_index_map.end() &&
        iter_trx_map->second > max_committed_index) {
      LogPluginErr(INFORMATION_LEVEL, ER_CONSENSUS_RECOVERY_TRX_LOGS,
                   "erase a prepared transaction from binlog transactions list",
                   xid, iter_trx_map->second,
                   enum_ha_recover_xa_state::PREPARED_IN_SE);
      commit_list.erase(iter);
    }
  }

  return max_committed_index;
}