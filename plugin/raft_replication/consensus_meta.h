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

#ifndef CONSENSUS_META_INCLUDE
#define CONSENSUS_META_INCLUDE
#include <atomic>

#include "objstore.h"
#include "sql/consensus/consensus_applier_info.h"
#include "sql/consensus/consensus_info.h"

class ConsensusMeta {
 public:
  ConsensusMeta()
      : inited(false),
        consensus_info(nullptr),
        consensus_applier_info(nullptr),
        cluster_info_objstore_initied(false),
        cluster_info_objstore(nullptr),
        cluster_info_on_objstore_inited(false),
        cluster_info_index(0),
        already_set_start_index(false),
        already_set_start_term(false) {}
  ~ConsensusMeta() {}

  int init();
  int cleanup();

  Consensus_info *get_consensus_info() { return consensus_info; }
  Consensus_applier_info *get_applier_info() { return consensus_applier_info; }

  void clear_already_set_start() {
    already_set_start_index = false;
    already_set_start_term = false;
  }

  // consensus_info
  int init_consensus_info();
  int update_consensus_info();
  int set_start_apply_index_if_need(uint64 consensus_index);
  int set_start_apply_term_if_need(uint64 consensus_term);

  int get_cluster_info(std::string &members_info, std::string &learners_info,
                         uint64 &index);
  int set_cluster_info(bool set_members, const std::string &members_info,
                       bool set_learners, const std::string &learners_info,
                       bool set_index, uint64 index);

 private:
  bool inited;
  Consensus_info *consensus_info;                  // consensus system info
  Consensus_applier_info *consensus_applier_info;  // consensus system info

  bool cluster_info_objstore_initied;
  objstore::ObjectStore *cluster_info_objstore;
  bool cluster_info_on_objstore_inited;
  std::string cluster_info_bucket;
  std::string cluster_info_path;
  uint64 cluster_info_index;

  // consensus_info
  std::atomic<bool> already_set_start_index;  // set at first downgrade, used to
                                              // set correct start apply index
  std::atomic<bool> already_set_start_term;   // set at first downgrade, used to
                                              // set correct start apply term

  int change_meta_if_needed();

  int init_objstore_for_cluster_info();
  int init_cluster_info_on_objstore();
  int read_cluster_info_on_objstore(std::string &members_info,
                                    std::string &learners_info, uint64 &index,
                                    size_t &number_of_lines);
  int store_cluster_info_on_objstore();
};

extern ConsensusMeta consensus_meta;

#endif // CONSENSUS_META_INCLUDE