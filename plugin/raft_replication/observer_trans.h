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

#ifndef CONSENSUS_OBSERVER_TRANS
#define CONSENSUS_OBSERVER_TRANS

#include "plugin.h"
#include "sql/replication.h"

/*
  Transaction lifecycle events observers.
*/
int consensus_replication_trans_before_dml(Trans_param *param, int &out);

int consensus_replication_trans_before_commit(Trans_param *param);

int consensus_replication_trans_before_rollback(Trans_param *param);

int consensus_replication_trans_after_commit(Trans_param *param);

int consensus_replication_trans_after_rollback(Trans_param *param);

int consensus_replication_trans_begin(Trans_param *param, int &out);

extern Trans_observer cr_trans_observer;

#endif /* CONSENSUS_OBSERVER_TRANS */
