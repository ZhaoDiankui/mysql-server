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

#include "consensus_log_index.h"
#include "plugin_psi.h"
#include "sql/log.h"

int ConsensusLogIndex::init() {
  DBUG_TRACE;
  mysql_mutex_init(key_mutex_ConsensusLog_index, &LOCK_consensuslog_index,
                   MY_MUTEX_INIT_FAST);
  inited = true;
  return 0;
}

int ConsensusLogIndex::cleanup() {
  DBUG_TRACE;
  if (inited) {
    mysql_mutex_destroy(&LOCK_consensuslog_index);
  }
  return 0;
}

int ConsensusLogIndex::add_to_index_list(uint64 start_index, ulong timestamp,
                                         const std::string &log_name,
                                         my_off_t log_size /* = 0 */,
                                         bool remove_dup /* = false */) {
  DBUG_TRACE;
  // init log_size to 0, used for automically purge, will be updated
  // when the first time it is calculated in purge condition check.
  // see: ConsensusLogIndex::update_log_size_by_name
  ConsensusLogIndexEntry entry = {
      start_index, timestamp, log_size, log_name, {}};
  mysql_mutex_lock(&LOCK_consensuslog_index);
  if (remove_dup) {
    auto iter = index_list.lower_bound(start_index);
    index_list.erase(iter, index_list.end());
  } else if (log_size > 0) {
    total_log_size += log_size;
  }
  index_list.insert(std::pair(start_index, entry));
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::clear_all() {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  total_log_size = 0;
  index_list.clear();
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::truncate_before(std::string &log_name) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  // Truncate newer log files. The iterator begins from the older to the newer.
  // Decrease total_log_size when iterating the entries that will be truncated.
  // It's different from truncate_after.
  for (auto iter = index_list.begin(); iter != index_list.end(); ++iter) {
    if (log_name == iter->second.file_name) {
      index_list.erase(index_list.begin(), iter);
      break;
    }
    if (total_log_size >= iter->second.log_size)
      total_log_size -= iter->second.log_size;
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::truncate_after(std::string &log_name) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  // Truncate newer log files. The iterator begins from the older to the newer.
  // Recompute total_log_size not decrease it, because we do not have to iterate
  // the entries that will be truncated. It's different from truncate_before.
  total_log_size = 0;
  for (auto iter = index_list.begin(); iter != index_list.end(); ++iter) {
    total_log_size += iter->second.log_size;
    if (log_name == iter->second.file_name) {
      index_list.erase(++iter, index_list.end());
      break;
    }
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

my_off_t ConsensusLogIndex::get_total_log_size() {
  DBUG_TRACE;
  my_off_t ret = 0;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  ret = total_log_size;
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return ret;
}

int ConsensusLogIndex::get_log_file_from_index(uint64 consensus_index,
                                               std::string &log_name,
                                               uint64 &start_index) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  if (index_list.empty() || consensus_index < index_list.begin()->first) {
    mysql_mutex_unlock(&LOCK_consensuslog_index);
    return -1;
  }
  /* Find the first greater element */
  auto iter = index_list.upper_bound(consensus_index);
  --iter;
  start_index = iter->second.index;
  log_name = iter->second.file_name;

  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::get_log_file_list(std::vector<std::string> &file_list) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  for (auto iter = index_list.begin(); iter != index_list.end(); ++iter) {
    file_list.push_back(iter->second.file_name);
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::get_log_file_entry_list(
    std::vector<ConsensusLogIndexEntry> &file_list) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  for (auto iter = index_list.begin(); iter != index_list.end(); ++iter) {
    file_list.push_back(iter->second);
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::get_first_log_should_purge_by_size(
    my_off_t purge_target_size, std::string &log_name, uint64 &index) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  my_off_t current_size = total_log_size;
  for (auto iter = index_list.begin(); iter != index_list.end(); ++iter) {
    current_size -= iter->second.log_size;
    if (current_size < purge_target_size) {
      log_name = iter->second.file_name;
      index = iter->second.index;
      break;
    }
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

int ConsensusLogIndex::get_first_log_should_purge_by_time(ulong timestamp,
                                                          std::string &log_name,
                                                          uint64 &index) {
  DBUG_TRACE;
  bool found = false;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  // purge the logs before the timestamp
  for (auto riter = index_list.rbegin(); riter != index_list.rend(); ++riter) {
    // find the first log whose timestamp is less than the target timestamp.
    // the logs before this log can be purged.
    if (riter->second.timestamp <= timestamp) {
      found = true;
      log_name = riter->second.file_name;
      index = riter->second.index;
      break;
    }
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return (!found);
}

uint64 ConsensusLogIndex::get_first_index() {
  DBUG_TRACE;
  uint64 first_index = 0;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  auto iter = index_list.begin();
  if (iter != index_list.end()) first_index = iter->second.index;
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return first_index;
}

uint64 ConsensusLogIndex::get_start_index_of_file(const std::string &log_name) {
  DBUG_TRACE;
  uint64 index = 0;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  for (auto riter = index_list.rbegin(); riter != index_list.rend(); ++riter) {
    if (log_name == riter->second.file_name) {
      index = riter->second.index;
      break;
    }
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return index;
}

int ConsensusLogIndex::get_lower_bound_pos_of_index(
    const uint64 start_index, const uint64 consensus_index, my_off_t &pos,
    bool &matched) {
  DBUG_TRACE;

  matched = false;

  mysql_mutex_lock(&LOCK_consensuslog_index);

  auto file_iter = index_list.upper_bound(start_index);
  if (file_iter == index_list.begin()) {
    pos = 0;
    mysql_mutex_unlock(&LOCK_consensuslog_index);
    return 0;
  }

  --file_iter;
  assert(file_iter->first == start_index);

  if (!file_iter->second.pos_map.empty()) {
    std::map<uint64, my_off_t> *pos_map = &file_iter->second.pos_map;
    auto iter = pos_map->upper_bound(consensus_index);
    if (iter != pos_map->begin()) {
      --iter;
      pos = iter->second;
      if (consensus_index == iter->first) matched = true;
    } else {
      pos = 0;
    }
  } else {
    pos = 0;
  }

  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

void ConsensusLogIndex::update_pos_map_by_start_index(uint64 start_index,
                                                      uint64 consensus_index,
                                                      my_off_t pos) {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_consensuslog_index);

  auto iter_next = index_list.upper_bound(start_index);
  if (iter_next == index_list.begin()) {
    mysql_mutex_unlock(&LOCK_consensuslog_index);
    return;
  }

  auto iter = std::prev(iter_next);
  assert(iter->first == start_index);

  assert(iter_next == index_list.end() ||
         consensus_index <= iter_next->second.index);
  iter->second.pos_map[consensus_index] = pos;

  mysql_mutex_unlock(&LOCK_consensuslog_index);
}

void ConsensusLogIndex::update_pos_map_by_file_name(std::string &log_name,
                                                    uint64 consensus_index,
                                                    my_off_t pos) {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_consensuslog_index);

  for (auto riter = index_list.rbegin(); riter != index_list.rend(); ++riter) {
    if (log_name == riter->second.file_name) {
      uint next_index = 0;

      if (riter != index_list.rbegin()) {
        auto iter_next = std::prev(riter);
        next_index = iter_next->second.index;
      }

      if (consensus_index >= riter->second.index &&
          (next_index == 0 || consensus_index <= next_index)) {
        riter->second.pos_map[consensus_index] = pos;
      }

      break;
    }
  }

  mysql_mutex_unlock(&LOCK_consensuslog_index);
}

int ConsensusLogIndex::truncate_pos_map_of_file(uint64 start_index,
                                                uint64 consensus_index) {
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_consensuslog_index);

  auto iter = index_list.upper_bound(start_index);
  if (iter == index_list.begin()) {
    mysql_mutex_unlock(&LOCK_consensuslog_index);
    return 1;
  }

  --iter;
  assert(iter->first == start_index);

  std::map<uint64, my_off_t> *pos_map = &iter->second.pos_map;
  pos_map->erase(pos_map->lower_bound(consensus_index), pos_map->end());

  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}

std::string ConsensusLogIndex::get_first_log_file_name() {
  std::string ret;
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_consensuslog_index);
  if (!index_list.empty()) ret = (index_list.begin())->second.file_name;
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return ret;
}

std::string ConsensusLogIndex::get_last_log_file_name() {
  std::string ret;
  DBUG_TRACE;

  mysql_mutex_lock(&LOCK_consensuslog_index);
  if (!index_list.empty()) ret = (index_list.rbegin())->second.file_name;
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return ret;
}

int ConsensusLogIndex::update_log_size_by_name(const std::string &log_name,
                                               my_off_t log_size) {
  DBUG_TRACE;
  mysql_mutex_lock(&LOCK_consensuslog_index);
  // A reverse iterator is used to update the log_size, because the log_size is
  // always updated when rotating the last log.
  for (auto iter = index_list.rbegin(); iter != index_list.rend(); ++iter) {
    if (log_name == iter->second.file_name) {
      if (iter->second.log_size != 0) {
        if (log_size > iter->second.log_size) {
          my_off_t increment = log_size - iter->second.log_size;
          // log_size has been set in recovery, reset it to the newest.
          iter->second.log_size = log_size;
          // only add the increment to total_log_size.
          total_log_size += increment;
        } else if (log_size < iter->second.log_size) {
          // some log entries may be truncated in recovery.
          my_off_t reduce = iter->second.log_size - log_size;
          iter->second.log_size = log_size;
          // reduce the total_log_size.
          total_log_size -= reduce;
        } else {
          // log_size doesn't change, do nothing
        }
      } else {
        iter->second.log_size = log_size;
        total_log_size += log_size;
      }
      break;
    }
  }
  mysql_mutex_unlock(&LOCK_consensuslog_index);
  return 0;
}
