/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SMARTENGINE_DICT_UTIL_H_
#define SMARTENGINE_DICT_UTIL_H_

#include <string>
#include "dd/types/schema.h"
#include "dd/types/table.h"
#include "dict/se_index.h"
#include "util/se_utils.h"
#include "db/db.h"

class Field;
class MDL_ticket;
struct TABLE;
namespace dd
{
namespace cache
{
  class Dictionary_client;
} // namespace cache
} // namespace dd

namespace smartengine
{

/*
  This is
  - the name of the default Column Family (the CF which stores indexes which
    didn't explicitly specify which CF they are in)
  - the name used to set the default column family parameter for per-cf
    arguments.
*/
extern const char *const DEFAULT_CF_NAME;

/*
  This is the databae_name of smartengine system table
*/
extern const char * DEFAULT_SYSTEM_DATABASE_NAME;

/*
  This is the table_name of smartengine system table
*/
extern const char * DEFAULT_SYSTEM_TABLE_NAME;

/*
  This is the name of the Column Family used for storing the data dictionary.
*/
extern const char *const DEFAULT_SYSTEM_SUBTABLE_NAME;

/*
  Now, we only use cf_id communicate with subtable in se, for system_cf, cf_id is 1,
  for other user subtable, cf_id is same with index_id, which is start from 256.
*/
extern const int DEFAULT_SYSTEM_SUBTABLE_ID;

/*
  This is the default system table space id, use for system subtable or other subtable which table space is shared
*/
extern const int DEFAULT_SYSTEM_TABLE_SPACE_ID;

/*
  This is the name of the hidden primary key for tables with no pk.
*/
extern const char *const HIDDEN_PK_NAME;

/*
  Name for the background thread.
*/
extern const char *const BG_THREAD_NAME;

/*
  Name for the renewal objstore lease lock thread.
*/
extern const char *const RENEW_LEASE_LOCK_THREAD_NAME;

/*
  Name for the drop index thread.
*/
extern const char *const INDEX_THREAD_NAME;

std::string& gen_se_supported_collation_string();

bool se_is_index_collation_supported(const my_core::Field *const field);

ulonglong se_get_int_col_max_value(const Field *field);

int advice_fix_index(TABLE *table,
                     int64_t keyno,
                     int64_t hidden_pk_id,
                     uchar *data,
                     const char *op);

int advice_del_index(TABLE *table,
                     int64_t keyno,
                     int64_t hidden_pk_id,
                     uchar *data);

int advice_add_index(TABLE *table,
                     int64_t keyno,
                     int64_t hidden_pk_id,
                     uchar *data);

db::Range get_range(uint32_t i,
                    uchar buf[SeKeyDef::INDEX_NUMBER_SIZE * 2],
                    int offset1,
                    int offset2);

db::Range get_range(const SeKeyDef &kd,
                    uchar buf[SeKeyDef::INDEX_NUMBER_SIZE * 2],
                    int offset1,
                    int offset2);

db::Range get_range(const SeKeyDef &kd, uchar buf[SeKeyDef::INDEX_NUMBER_SIZE * 2]);

bool looks_like_per_index_cf_typo(const char *const name);

struct SeDdHelper {
  // traverse over data dictionary of all se tables
  static bool traverse_all_se_tables(
      THD *thd, bool exit_on_failure, ulong lock_timeout,
      std::function<bool(const dd::Schema *, const dd::Table *)> &&visitor);

  // get all schemas from data dictionary and build map from schema_id to schema
  static bool get_schema_id_map(
      dd::cache::Dictionary_client* client,
      std::vector<const dd::Schema*>& all_schemas,
      std::map<dd::Object_id, const dd::Schema*>& schema_id_map);

  // get all SE subtables from data dictionary and build map from id to name
  static bool get_se_subtable_map(THD* thd,
      std::map<uint32_t, std::pair<std::string, std::string>>& subtable_map);

  // lock and acquire dd::Table object with engine() == XENIGNE
  // if ouput dd_table isn't null, caller needs to release the mdl lock
  static bool acquire_se_table(THD* thd, ulong lock_timeout,
      const char* schema_name, const char* table_name,
      const dd::Table*& dd_table, MDL_ticket*& mdl_ticket);

  // get id of all SE subtables from data dictionary
  static bool get_se_subtable_ids(THD* thd, ulong lock_timeout,
      std::set<uint32_t>& subtable_ids);
};

extern bool is_smartengine_system_database(const char *database_name);

} // namespace smartengine

#endif
