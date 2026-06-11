/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2014, SkySQL Ab

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
#include "util/se_comparator.h"
#include "table/table.h"

namespace smartengine {

/*
  Per-column family options configs.

  Per-column family option can be set
  - Globally (the same value applies to all column families)
  - Per column family: there is a {cf_name -> value} map,
    and also there is a default value which applies to column
    families not found in the map.
*/
class SeSubtableOptions {
public:
  SeSubtableOptions(const SeSubtableOptions &) = delete;
  SeSubtableOptions &operator=(const SeSubtableOptions &) = delete;
  SeSubtableOptions() = default;

  bool init(const table::BlockBasedTableOptions &table_options,
            const common::ColumnFamilyOptions& default_cf_options);

  const common::ColumnFamilyOptions &get_defaults() const { return m_default_cf_opts; }

  static const util::Comparator *get_cf_comparator(const std::string &cf_name);

  void get_cf_options(common::ColumnFamilyOptions *const opts);
  //TODO (Zhao Dongsheng) : deprecated
  void get_cf_options(const std::string &cf_name, common::ColumnFamilyOptions *const opts);

private:
  static SePrimaryKeyComparator s_pk_comparator;
  static SePrimaryKeyReverseComparator s_rev_pk_comparator;

  common::ColumnFamilyOptions m_default_cf_opts;
};

} //namespace smartengine
