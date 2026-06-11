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
#pragma once

#include <string>
#include <vector>
#include "util/se_utils.h"

namespace smartengine
{

struct SeIndexStats {
  enum {
    INDEX_STATS_VERSION_INITIAL = 1,
    INDEX_STATS_VERSION_ENTRY_TYPES = 2,
  };
  GL_INDEX_ID m_gl_index_id;
  int64_t m_data_size, m_rows, m_actual_disk_size;
  int64_t m_entry_deletes, m_entry_single_deletes;
  int64_t m_entry_merges, m_entry_others;
  std::vector<int64_t> m_distinct_keys_per_prefix;
  std::string m_name; // name is not persisted

  static std::string materialize(const std::vector<SeIndexStats> &stats,
                                 const float card_adj_extra);
  static int unmaterialize(const std::string &s,
                           std::vector<SeIndexStats> *const ret);

  SeIndexStats() : SeIndexStats({0, 0}) {}
  explicit SeIndexStats(GL_INDEX_ID gl_index_id)
      : m_gl_index_id(gl_index_id),
        m_data_size(0),
        m_rows(0),
        m_actual_disk_size(0),
        m_entry_deletes(0),
        m_entry_single_deletes(0),
        m_entry_merges(0),
        m_entry_others(0)
  {}

  void merge(const SeIndexStats &s,
             const bool &increment = true,
             const int64_t &estimated_data_len = 0);

  void update(const SeIndexStats &index_stats, const int64_t &estimated_data_len = 0);
  void reset()
  {
    m_data_size = 0;
    m_rows = 0;
    m_actual_disk_size = 0;
    m_entry_deletes = 0;
    m_entry_single_deletes = 0;
    m_entry_merges = 0;
    m_entry_others = 0;
  }
};

} //namespace smartengine
