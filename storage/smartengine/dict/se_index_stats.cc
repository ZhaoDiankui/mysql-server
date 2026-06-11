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

#include "se_index_stats.h"
#include <string>
#include <vector>
#include "se_buff.h"
#include "util/se_utils.h"

namespace smartengine
{

/*
  Serializes an array of SeIndexStats into a network string.
*/
std::string SeIndexStats::materialize(const std::vector<SeIndexStats> &stats,
                                      const float card_adj_extra)
{
  String ret;
  se_netstr_append_uint16(&ret, INDEX_STATS_VERSION_ENTRY_TYPES);
  for (const auto &i : stats) {
    se_netstr_append_uint32(&ret, i.m_gl_index_id.cf_id);
    se_netstr_append_uint32(&ret, i.m_gl_index_id.index_id);
    assert(sizeof i.m_data_size <= 8);
    se_netstr_append_uint64(&ret, i.m_data_size);
    se_netstr_append_uint64(&ret, i.m_rows);
    se_netstr_append_uint64(&ret, i.m_actual_disk_size);
    se_netstr_append_uint64(&ret, i.m_distinct_keys_per_prefix.size());
    se_netstr_append_uint64(&ret, i.m_entry_deletes);
    se_netstr_append_uint64(&ret, i.m_entry_single_deletes);
    se_netstr_append_uint64(&ret, i.m_entry_merges);
    se_netstr_append_uint64(&ret, i.m_entry_others);
    for (const auto &num_keys : i.m_distinct_keys_per_prefix) {
      const float upd_num_keys = num_keys * card_adj_extra;
      se_netstr_append_uint64(&ret, static_cast<int64_t>(upd_num_keys));
    }
  }

  return std::string((char *)ret.ptr(), ret.length());
}

/**
  @brief
  Reads an array of SeIndexStats from a string.
  @return HA_EXIT_FAILURE if it detects any inconsistency in the input
  @return HA_EXIT_SUCCESS if completes successfully
*/
int SeIndexStats::unmaterialize(const std::string &s, std::vector<SeIndexStats> *const ret)
{
  const uchar *p = se_std_str_to_uchar_ptr(s);
  const uchar *const p2 = p + s.size();

  assert(ret != nullptr);

  if (p + 2 > p2) {
    return HA_EXIT_FAILURE;
  }

  const int version = se_netbuf_read_uint16(&p);
  SeIndexStats stats;
  // Make sure version is within supported range.
  if (version < INDEX_STATS_VERSION_INITIAL ||
      version > INDEX_STATS_VERSION_ENTRY_TYPES) {
    // NO_LINT_DEBUG
    sql_print_error("Index stats version %d was outside of supported range. "
                    "This should not happen so aborting the system.",
                    version);
    abort_with_stack_traces();
  }

  size_t needed = sizeof(stats.m_gl_index_id.cf_id) +
                  sizeof(stats.m_gl_index_id.index_id) +
                  sizeof(stats.m_data_size) + sizeof(stats.m_rows) +
                  sizeof(stats.m_actual_disk_size) + sizeof(uint64);
  if (version >= INDEX_STATS_VERSION_ENTRY_TYPES) {
    needed += sizeof(stats.m_entry_deletes) +
              sizeof(stats.m_entry_single_deletes) +
              sizeof(stats.m_entry_merges) + sizeof(stats.m_entry_others);
  }

  while (p < p2) {
    if (p + needed > p2) {
      return HA_EXIT_FAILURE;
    }
    se_netbuf_read_gl_index(&p, &stats.m_gl_index_id);
    stats.m_data_size = se_netbuf_read_uint64(&p);
    stats.m_rows = se_netbuf_read_uint64(&p);
    stats.m_actual_disk_size = se_netbuf_read_uint64(&p);
    stats.m_distinct_keys_per_prefix.resize(se_netbuf_read_uint64(&p));
    if (version >= INDEX_STATS_VERSION_ENTRY_TYPES) {
      stats.m_entry_deletes = se_netbuf_read_uint64(&p);
      stats.m_entry_single_deletes = se_netbuf_read_uint64(&p);
      stats.m_entry_merges = se_netbuf_read_uint64(&p);
      stats.m_entry_others = se_netbuf_read_uint64(&p);
    }
    if (p +
            stats.m_distinct_keys_per_prefix.size() *
                sizeof(stats.m_distinct_keys_per_prefix[0]) >
        p2) {
      return HA_EXIT_FAILURE;
    }
    for (std::size_t i = 0; i < stats.m_distinct_keys_per_prefix.size(); i++) {
      stats.m_distinct_keys_per_prefix[i] = se_netbuf_read_uint64(&p);
    }
    ret->push_back(stats);
  }
  return HA_EXIT_SUCCESS;
}

/*
  Merges one SeIndexStats into another. Can be used to come up with the stats
  for the index based on stats for each sst
*/
void SeIndexStats::merge(const SeIndexStats &s,
                         const bool &increment,
                         const int64_t &estimated_data_len)
{
  std::size_t i;

  assert(estimated_data_len >= 0);

  m_gl_index_id = s.m_gl_index_id;
  if (m_distinct_keys_per_prefix.size() < s.m_distinct_keys_per_prefix.size()) {
    m_distinct_keys_per_prefix.resize(s.m_distinct_keys_per_prefix.size());
  }
  if (increment) {
    m_rows += s.m_rows;
    m_data_size += s.m_data_size;
    /*
      The Data_length and Avg_row_length are trailing statistics, meaning
      they don't get updated for the current SST until the next SST is
      written.  So, if se reports the data_length as 0,
      we make a reasoned estimate for the data_file_length for the
      index in the current SST.
    */
    m_actual_disk_size += s.m_actual_disk_size ? s.m_actual_disk_size
                                               : estimated_data_len * s.m_rows;
    m_entry_deletes += s.m_entry_deletes;
    m_entry_single_deletes += s.m_entry_single_deletes;
    m_entry_merges += s.m_entry_merges;
    m_entry_others += s.m_entry_others;
    if (s.m_distinct_keys_per_prefix.size() > 0) {
      for (i = 0; i < s.m_distinct_keys_per_prefix.size(); i++) {
        m_distinct_keys_per_prefix[i] += s.m_distinct_keys_per_prefix[i];
      }
    } else {
      for (i = 0; i < m_distinct_keys_per_prefix.size(); i++) {
        m_distinct_keys_per_prefix[i] +=
            s.m_rows >> (m_distinct_keys_per_prefix.size() - i - 1);
      }
    }
  } else {
    m_rows -= s.m_rows;
    m_data_size -= s.m_data_size;
    m_actual_disk_size -= s.m_actual_disk_size ? s.m_actual_disk_size
                                               : estimated_data_len * s.m_rows;
    m_entry_deletes -= s.m_entry_deletes;
    m_entry_single_deletes -= s.m_entry_single_deletes;
    m_entry_merges -= s.m_entry_merges;
    m_entry_others -= s.m_entry_others;
    if (s.m_distinct_keys_per_prefix.size() > 0) {
      for (i = 0; i < s.m_distinct_keys_per_prefix.size(); i++) {
        m_distinct_keys_per_prefix[i] -= s.m_distinct_keys_per_prefix[i];
      }
    } else {
      for (i = 0; i < m_distinct_keys_per_prefix.size(); i++) {
        m_distinct_keys_per_prefix[i] -=
            s.m_rows >> (m_distinct_keys_per_prefix.size() - i - 1);
      }
    }
  }
  // avoid negative stats
  if (m_rows < 0) {
    m_rows = 0;
  }
  if (m_actual_disk_size < 0) {
    m_actual_disk_size = 0;
  }
}

void SeIndexStats::update(const SeIndexStats &s, const int64_t &estimated_data_len)
{
  std::size_t i;

  assert(estimated_data_len >= 0);

  m_gl_index_id = s.m_gl_index_id;
  if (m_distinct_keys_per_prefix.size() < s.m_distinct_keys_per_prefix.size()) {
    m_distinct_keys_per_prefix.resize(s.m_distinct_keys_per_prefix.size());
  }
  m_rows = s.m_rows;
  m_data_size = s.m_data_size;

  /*
    The Data_length and Avg_row_length are trailing statistics, meaning
    they don't get updated for the current SST until the next SST is
    written.  So, if se reports the data_length as 0,
    we make a reasoned estimate for the data_file_length for the
    index in the current SST.
  */
  m_actual_disk_size = s.m_actual_disk_size ? s.m_actual_disk_size
                                              : estimated_data_len * s.m_rows;
  m_entry_deletes = s.m_entry_deletes;
  m_entry_single_deletes = s.m_entry_single_deletes;
  m_entry_merges = s.m_entry_merges;
  m_entry_others = s.m_entry_others;
  if (s.m_distinct_keys_per_prefix.size() > 0) {
    for (i = 0; i < s.m_distinct_keys_per_prefix.size(); i++) {
      m_distinct_keys_per_prefix[i] += s.m_distinct_keys_per_prefix[i];
    }
  } else {
    for (i = 0; i < m_distinct_keys_per_prefix.size(); i++) {
      m_distinct_keys_per_prefix[i] +=
          s.m_rows >> (m_distinct_keys_per_prefix.size() - i - 1);
    }
  }
  // avoid negative stats
  if (m_rows < 0) {
    m_rows = 0;
  }
  if (m_actual_disk_size < 0) {
    m_actual_disk_size = 0;
  }
}

} //namespace smartengine
