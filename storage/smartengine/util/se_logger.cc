/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2015, Facebook, Inc.

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
#include "se_logger.h"
#include "se_hton.h"
#include "sql/mysqld.h"
#include "core/logger/logger.h"

void mysql_set_se_info_log_level(ulong lvl)
{
  smartengine::logger::Logger::get_log().set_log_level(smartengine::get_se_log_level(lvl));
}

bool mysql_reinit_se_log()
{
  int log_init_ret = 0;
  auto log_level = smartengine::get_se_log_level(log_error_verbosity);
  if ((NULL != log_error_dest) && (0 != strcmp(log_error_dest, "stderr")) &&
      smartengine::logger::Logger::get_log().is_inited()) {
    fsync(STDERR_FILENO);
    log_init_ret = smartengine::logger::Logger::get_log().reinit(STDERR_FILENO, log_level);
  }

  return (0 != log_init_ret);
}

