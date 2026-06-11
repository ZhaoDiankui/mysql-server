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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation // gcc: Class implementation
#endif

#include "se_cf_options.h"
#include <string>
#include "dict/se_cf_manager.h"

namespace smartengine
{
SePrimaryKeyComparator SeSubtableOptions::s_pk_comparator;
SePrimaryKeyReverseComparator SeSubtableOptions::s_rev_pk_comparator;

bool SeSubtableOptions::init(const table::BlockBasedTableOptions &table_options,
                             const common::ColumnFamilyOptions& default_cf_options)
{
  // ToDo: validate default_cf_options
  m_default_cf_opts = default_cf_options;
  m_default_cf_opts.comparator = &s_pk_comparator;

  m_default_cf_opts.table_factory.reset(table::NewExtentBasedTableFactory(table_options));

  return true;
}




const util::Comparator *SeSubtableOptions::get_cf_comparator(const std::string &cf_name)
{
  if (SeSubtableManager::is_cf_name_reverse(cf_name.c_str())) {
    return &s_rev_pk_comparator;
  } else {
    return &s_pk_comparator;
  }
}

void SeSubtableOptions::get_cf_options(common::ColumnFamilyOptions *const opts)
{
  *opts = m_default_cf_opts;
  // Set the comparator according to 'rev:'
  opts->comparator = &s_pk_comparator;
}

// TODO (Zhao Dongsheng) : deprecated.
void SeSubtableOptions::get_cf_options(const std::string &cf_name, common::ColumnFamilyOptions *const opts)
{
  *opts = m_default_cf_opts;
  // Set the comparator according to 'rev:'
  opts->comparator = get_cf_comparator(cf_name);
}

} //namespace smartengine
