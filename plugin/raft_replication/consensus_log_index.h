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

#ifndef CONSENSUS_LOG_INDEX_INCLUDE
#define CONSENSUS_LOG_INDEX_INCLUDE

#include <deque>
#include <map>
#include <string>
#include <vector>
#include "mysql/psi/mysql_mutex.h"
#include "sql/mysqld.h"

struct ConsensusLogIndexEntry {
  uint64 index;
  ulong timestamp;
  my_off_t log_size;
  std::string file_name;
  std::map<uint64, my_off_t> pos_map;
};

class ConsensusLogIndex {
 public:
  ConsensusLogIndex() : inited(false), total_log_size(0) {}
  ~ConsensusLogIndex() {}

  int init();
  int cleanup();

  int add_to_index_list(uint64 consensus_index, ulong timestamp,
                        const std::string &log_name, my_off_t log_size = 0,
                        bool remove_dup = false);
  int truncate_before(std::string &log_name);  // retain log_name
  int truncate_after(std::string &log_name);   // retain log_name
  int clear_all();
  my_off_t get_total_log_size();

  int get_log_file_from_index(uint64 consensus_index, std::string &log_name,
                              uint64 &start_index);
  int get_log_file_list(std::vector<std::string> &file_list);
  int get_log_file_entry_list(std::vector<ConsensusLogIndexEntry> &file_list);
  int get_first_log_should_purge_by_size(my_off_t purge_target_size,
                                         std::string &log_name, uint64 &index);
  int get_first_log_should_purge_by_time(ulong timestamp /* in seconds */,
                                         std::string &log_name, uint64 &index);
  int update_log_size_by_name(const std::string &log_name, my_off_t log_size);
  uint64 get_start_index_of_file(const std::string &log_name);
  std::string get_last_log_file_name();
  std::string get_first_log_file_name();

  void update_pos_map_by_file_name(std::string &log_name,
                                   uint64 consensus_index, my_off_t pos);
  void update_pos_map_by_start_index(uint64 start_index, uint64 consensus_index,
                                     my_off_t pos);
  int get_lower_bound_pos_of_index(uint64 start_index, uint64 consensus_index,
                                   my_off_t &pos, bool &matched);
  int truncate_pos_map_of_file(uint64 start_index, uint64 consensus_index);

  uint64 get_first_index();

 private:
  bool inited;
  my_off_t total_log_size;
  mysql_mutex_t LOCK_consensuslog_index;
  std::multimap<uint64, ConsensusLogIndexEntry>
      index_list;  // hold all the binlogs' index
};

#endif
