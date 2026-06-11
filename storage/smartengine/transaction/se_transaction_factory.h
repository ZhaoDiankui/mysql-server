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

#ifndef SMARTENGINE_TRANSACTION_FACTORY_H_
#define SMARTENGINE_TRANSACTION_FACTORY_H_

#include <vector>
#include "se_transaction_list_walker.h"

class THD;
struct handlerton;

namespace smartengine
{
class SeTransaction;

SeTransaction *&get_tx_from_thd(THD *const thd);

SeTransaction *get_or_create_tx(THD *const thd);

void se_register_tx(handlerton *const hton, THD *const thd, SeTransaction *const tx);

std::vector<SeTrxInfo> se_get_all_trx_info();

} // namespace smartengine

#endif // end of SMARTENGINE_TRANSACTION_FACTORY_H_
