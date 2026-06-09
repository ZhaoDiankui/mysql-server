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

#ifndef CONSENSUS_I_S_INCLUDED
#define CONSENSUS_I_S_INCLUDED

extern struct st_mysql_plugin i_s_wesql_cluster_global;
extern struct st_mysql_plugin i_s_wesql_cluster_local;
extern struct st_mysql_plugin i_s_wesql_cluster_health;
extern struct st_mysql_plugin i_s_wesql_cluster_learner_source;
extern struct st_mysql_plugin i_s_wesql_cluster_prefetch_channel;
extern struct st_mysql_plugin i_s_wesql_cluster_consensus_status;
extern struct st_mysql_plugin i_s_wesql_cluster_consensus_membership_change;

#endif /* CONSENSUS_I_S_INCLUDED */
