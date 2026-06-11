/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012, Monty Program Ab

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

#ifndef SMARTENGINE_SYSTEM_VARS_H_
#define SMARTENGINE_SYSTEM_VARS_H_

#include "util/se_mutex_wrapper.h"
#include "table/table.h"

struct SYS_VAR;
class THD;

namespace smartengine
{
const uint64_t DEFAULT_SE_CONCURRENT_WRITABLE_FILE_BUFFER_NUMBER = 4;

const uint64_t DEFAULT_SE_CONCURRENT_WRITABLE_FILE_SINGLE_BUFFER_SIZE = 64 * 1024;

const uint64_t DEFAULT_SE_CONCURRENT_WRITABLE_FILE_BUFFER_SWITCH_LIMIT = 32 * 1024;

extern mysql_mutex_t se_sysvars_mutex;

extern char *se_data_dir;

extern char *se_wal_dir;

extern char *se_persistent_cache_dir;

extern char se_backup_status[][16];

extern table::BlockBasedTableOptions se_tbl_options;

extern char *se_cf_compression_per_level;

extern char *se_cf_compression_opts;

extern char *se_hotbackup_name;

extern uint32_t se_flush_log_at_trx_commit;

extern bool se_enable_2pc;

extern common::DBOptions se_db_options;

extern long long se_row_cache_size;

extern long long se_block_cache_size;

extern common::ColumnFamilyOptions se_default_cf_options;

extern uint32_t se_wal_recovery_mode;

extern bool se_parallel_wal_recovery;

extern uint32_t se_parallel_recovery_thread_num;

extern bool se_pause_background_work;

extern bool se_flush_memtable_var;

extern long long se_compact_cf_id;

extern uint32_t se_disable_online_ddl;

extern bool se_disable_instant_ddl;

extern bool se_disable_parallel_ddl;

extern ulong se_sort_buffer_size;

extern bool opt_purge_invalid_subtable_bg;

extern unsigned long se_rate_limiter_bytes_per_sec;

extern int32_t se_shrink_table_space;

extern long long se_pending_shrink_subtable_id;

extern bool se_strict_collation_check;

extern char *se_strict_collation_exceptions;

extern uint64_t se_query_trace_sum;

extern bool se_query_trace_print_slow;

extern double se_query_trace_threshold_time;

//TODO:Zhao Dongsheng deprecated
extern bool se_rpl_skip_tx_api_var;

extern bool se_skip_unique_key_check_in_boost_insert;

extern uint64_t se_persistent_cache_mode;

void se_set_collation_exception_list(const char *const exception_list);

ulong se_thd_lock_wait_timeout(THD *thd);

bool se_thd_deadlock_detect(THD *thd);

ulong se_thd_max_row_locks(THD *thd);

bool se_thd_lock_scanned_rows(THD *thd);

ulong se_thd_bulk_load_size(THD *thd);

bool se_thd_write_disable_wal(THD *thd);

bool se_thd_unsafe_for_binlog(THD *thd);

ulong se_thd_parallel_read_threads(THD *thd);

extern SYS_VAR **se_system_vars_export;
} // namespace smartengine

#endif // end of SMARTENGINE_SYSTEM_VARS_H_
