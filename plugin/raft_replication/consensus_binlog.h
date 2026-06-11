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

#ifndef CONSENSUS_PLUGIN_BINLOG_INCLUDE
#define CONSENSUS_PLUGIN_BINLOG_INCLUDE

#include "sql/binlog.h"
#include "sql/consensus_log_event.h"
#include "sql/log_event.h"

class binlog_cache_data;
class ConsensusLogIndex;
class IO_CACHE_binlog_cache_storage;
struct ConsensusLogEntry;

void consensus_before_commit(THD *thd);
int write_binlog_cache_to_consensus_log(THD *thd, MYSQL_BIN_LOG *binlog,
                                        Gtid_log_event &gtid_event,
                                        binlog_cache_data *cache_data,
                                        bool have_checksum);
int consensus_append_log_entry(MYSQL_BIN_LOG *binlog, ConsensusLogEntry &log,
                               uint64 *index, Relay_log_info *rli,
                               bool with_check = false);
int consensus_append_multi_log_entries(MYSQL_BIN_LOG *binlog,
                                       std::vector<ConsensusLogEntry> &logs,
                                       uint64 *max_index, Relay_log_info *rli);
uint64 consensus_get_trx_end_index(ConsensusLogIndex *log_file_index,
                                   uint64 firstIndex);
int consensus_get_log_entry(ConsensusLogIndex *log_file_index,
                            uint64 consensus_index, uint64 *consensus_term,
                            std::string &log_content, bool *outer, uint *flag,
                            uint64 *checksum, bool need_content);
int consensus_get_log_position(ConsensusLogIndex *log_file_index,
                               uint64 consensus_index, char *log_name,
                               my_off_t *pos);
int consensus_get_log_end_position(ConsensusLogIndex *log_file_index,
                                   uint64 consensus_index, char *log_name,
                                   my_off_t *pos);
int consensus_find_log_by_index(ConsensusLogIndex *log_file_index,
                                uint64 consensus_index, std::string &file_name,
                                uint64 &start_index);
int consensus_prefetch_log_entries(THD *thd, uint64 channel_id,
                                   uint64 consensus_index);
int consensus_find_pos_by_index(ConsensusLogIndex *log_file_index,
                                const char *file_name, const uint64 start_index,
                                const uint64 consensus_index, my_off_t *pos);
int consensus_get_next_index(const char *file_name, bool skip_large_trx,
                             const ulong stop_datetime, const my_off_t stop_pos,
                             bool &reached_stop_point, uint64 &current_term);

int consensus_get_last_index_of_term(uint64 term, uint64 &last_index);

int consensus_build_log_file_index(MYSQL_BIN_LOG *binlog);
int copy_from_consensus_log_cache(IO_CACHE_binlog_cache_storage *from,
                                  uchar *to, my_off_t max_len);

int write_rotate_event_into_consensus_log(MYSQL_BIN_LOG *binlog);

void update_pos_map_by_start_index(ConsensusLogIndex *log_file_index,
                                   const uint64 start_index,
                                   Consensus_log_event *ev, my_off_t start_pos);

int rotate_consensus_log(THD *thd, bool force_rotate);
int purge_consensus_logs_on_conditions(ulong purge_time, ulong purge_size,
                                       const char *to_log, bool auto_purge);

int reencrypt_consensus_logs();

int consensus_open_archive_log(uint64 first_index, uint64 current_index);

bool consensus_show_log_events(THD *thd);

bool consensus_show_logs(THD *thd);
#endif