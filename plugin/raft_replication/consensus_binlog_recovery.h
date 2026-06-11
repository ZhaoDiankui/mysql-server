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

#ifndef CONSENSUS_BINLOG_RECOVERY_INCLUDE
#define CONSENSUS_BINLOG_RECOVERY_INCLUDE

#include "my_inttypes.h"

class MYSQL_BIN_LOG;

extern int consensus_binlog_recovery(MYSQL_BIN_LOG *binlog,
                                     bool has_ha_recover_end,
                                     uint64 ha_recover_end_index,
                                     char *binlog_end_file,
                                     my_off_t *binlog_end_pos);
#endif
