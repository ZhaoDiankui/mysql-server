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

#pragma once

#include "ha_smartengine.h"

namespace smartengine {
namespace common {
class Slice;
}
}


namespace smartengine {
//TODO determin which operations are online/offline/rebuild/norebuild//not supported

/** Operations for creating secondary indexes (no rebuild needed) */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_ONLINE_CREATE =
    Alter_inplace_info::ADD_INDEX | Alter_inplace_info::ADD_UNIQUE_INDEX;

/** Operations for rebuilding a table in place */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_ALTER_REBUILD =
    Alter_inplace_info::ADD_PK_INDEX | Alter_inplace_info::DROP_PK_INDEX |
    Alter_inplace_info::CHANGE_CREATE_OPTION
    /* CHANGE_CREATE_OPTION needs to check innobase_need_rebuild() */
    | Alter_inplace_info::ALTER_COLUMN_NULLABLE |
    Alter_inplace_info::ALTER_COLUMN_NOT_NULLABLE |
    Alter_inplace_info::ALTER_STORED_COLUMN_ORDER |
    Alter_inplace_info::DROP_STORED_COLUMN |
    Alter_inplace_info::ADD_STORED_BASE_COLUMN
    /* ADD_STORED_BASE_COLUMN needs to check innobase_need_rebuild() */
    | Alter_inplace_info::RECREATE_TABLE;

/** Operations that require changes to data */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_ALTER_DATA =
    SE_ONLINE_CREATE | SE_ALTER_REBUILD;

/** Operations for altering a table that se does not care about */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_INPLACE_IGNORE =
    Alter_inplace_info::ALTER_COLUMN_DEFAULT |
    Alter_inplace_info::ALTER_COLUMN_COLUMN_FORMAT |
    Alter_inplace_info::ALTER_COLUMN_STORAGE_TYPE |
    Alter_inplace_info::ALTER_RENAME | Alter_inplace_info::CHANGE_INDEX_OPTION |
    Alter_inplace_info::ADD_CHECK_CONSTRAINT |
    Alter_inplace_info::DROP_CHECK_CONSTRAINT |
    Alter_inplace_info::SUSPEND_CHECK_CONSTRAINT;

/** Operations on foreign key definitions (changing the schema only) */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_FOREIGN_OPERATIONS =
    Alter_inplace_info::DROP_FOREIGN_KEY | Alter_inplace_info::ADD_FOREIGN_KEY;

/** Operations that se cares about and can perform without rebuild */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_ALTER_NOREBUILD =
    SE_ONLINE_CREATE |
    Alter_inplace_info::DROP_INDEX | Alter_inplace_info::DROP_UNIQUE_INDEX |
    Alter_inplace_info::RENAME_INDEX | Alter_inplace_info::ALTER_COLUMN_NAME |
    Alter_inplace_info::ALTER_COLUMN_EQUAL_PACK_LENGTH |
    Alter_inplace_info::ALTER_INDEX_COMMENT |
    Alter_inplace_info::ALTER_COLUMN_INDEX_LENGTH;

/* Operations about partition */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_PARTITION_OPERATIONS =
    Alter_inplace_info::ADD_PARTITION | Alter_inplace_info::DROP_PARTITION |
    Alter_inplace_info::ALTER_PARTITION | Alter_inplace_info::COALESCE_PARTITION |
    Alter_inplace_info::REORGANIZE_PARTITION | Alter_inplace_info::ALTER_TABLE_REORG |
    Alter_inplace_info::ALTER_REBUILD_PARTITION |
    Alter_inplace_info::ALTER_REMOVE_PARTITIONING |
    Alter_inplace_info::ALTER_ALL_PARTITION;

/* OPerations about generated column */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_GCOL_OPERTIONS =
    Alter_inplace_info::ADD_VIRTUAL_COLUMN |
    Alter_inplace_info::ADD_STORED_GENERATED_COLUMN |
    Alter_inplace_info::DROP_VIRTUAL_COLUMN |
    Alter_inplace_info::ALTER_VIRTUAL_COLUMN_ORDER |
    Alter_inplace_info::VIRTUAL_GCOL_REEVAL |
    Alter_inplace_info::VALIDATE_VIRTUAL_COLUMN;

/* Alteration is not supported by SE */
static const Alter_inplace_info::HA_ALTER_FLAGS SE_ALTER_NOT_SUPPORTED =
    SE_FOREIGN_OPERATIONS | SE_PARTITION_OPERATIONS |
    SE_GCOL_OPERTIONS |
    Alter_inplace_info::ADD_SPATIAL_INDEX;

/*
  Helper class for in-place alter, for storing handler context between inplace
  alter calls
*/
struct SeInplaceAlterCtx : public my_core::inplace_alter_handler_ctx {
  /* The new table definition */
  std::shared_ptr<SeTableDef> m_new_tdef;

  /* Stores the original key definitions */
  std::shared_ptr<SeKeyDef> *const m_old_key_descr;

  /* Stores the new key definitions */
  std::shared_ptr<SeKeyDef> *m_new_key_descr;

  /* Stores the old number of key definitions */
  const uint m_old_n_keys;

  /* Stores the new number of key definitions */
  const uint m_new_n_keys;

  /* Stores the added key glids */
  const std::unordered_set<std::shared_ptr<SeKeyDef>> m_added_indexes;

  /* Stores the dropped key glids */
  const std::unordered_set<GL_INDEX_ID> m_dropped_index_ids;

  /* Stores number of keys to add */
  const uint m_n_added_keys;

  /* Stores number of keys to drop */
  const uint m_n_dropped_keys;

  /* rebuild flag */
  const bool m_rebuild;

  /* for no-rebuild online ddl */
  SeInplaceAlterCtx(
      SeTableDef *new_tdef, std::shared_ptr<SeKeyDef> *old_key_descr,
      std::shared_ptr<SeKeyDef> *new_key_descr, uint old_n_keys,
      uint new_n_keys,
      std::unordered_set<std::shared_ptr<SeKeyDef>> added_indexes,
      std::unordered_set<GL_INDEX_ID> dropped_index_ids, uint n_added_keys,
      uint n_dropped_keys)
      : my_core::inplace_alter_handler_ctx(), m_new_tdef(new_tdef),
        m_old_key_descr(old_key_descr), m_new_key_descr(new_key_descr),
        m_old_n_keys(old_n_keys), m_new_n_keys(new_n_keys),
        m_added_indexes(added_indexes), m_dropped_index_ids(dropped_index_ids),
        m_n_added_keys(n_added_keys), m_n_dropped_keys(n_dropped_keys), m_rebuild(false) {}

  /* for rebuild-copy online ddl */
  explicit SeInplaceAlterCtx(SeTableDef *new_tdef)
      : my_core::inplace_alter_handler_ctx(), m_new_tdef(new_tdef),
        m_old_key_descr(nullptr), m_new_key_descr(nullptr),
        m_old_n_keys(0), m_new_n_keys(0),
        m_n_added_keys(0), m_n_dropped_keys(0), m_rebuild(true) {}

  ~SeInplaceAlterCtx() {}

private:
  /* Disable Copying */
  SeInplaceAlterCtx(const SeInplaceAlterCtx &);
  SeInplaceAlterCtx &operator=(const SeInplaceAlterCtx &);
};

struct SeInplaceDdlDictInfo {

  SeInplaceDdlDictInfo()
      : m_encoder_arr(nullptr), m_null_bytes_in_rec(0),
        m_fields_no_needed_to_decode(0), m_maybe_unpack_info(false),
        m_instant_ddl_info(nullptr), m_verify_row_debug_checksums(false),
        m_row_checksums_checked(0) {}

  ~SeInplaceDdlDictInfo() {
    m_decoders_vect.clear();

    my_free(m_col_map);
    m_col_map = nullptr;

    my_free(m_col_map_rev);
    m_col_map_rev = nullptr;

    my_free(m_encoder_arr);
    m_encoder_arr = nullptr;
  }

  // dictionary used to convert to se-value
  SeFieldEncoder *m_encoder_arr;

  uint m_null_bytes_in_rec;

  uint32_t m_fields_no_needed_to_decode;

  bool m_maybe_unpack_info;

  std::vector<READ_FIELD> m_decoders_vect;

  //col_map from new_col to old_col
  uint *m_col_map;

  //col_map_rev from old_col to new_col
  uint *m_col_map_rev;

  InstantDDLInfo *m_instant_ddl_info;

  bool m_verify_row_debug_checksums;

  my_core::ha_rows m_row_checksums_checked;
};
} //namespace smartengine
