/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012,2013 Monty Program Ab

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
#ifndef SE_HANDLER_DICT_DICT_STRUCT_H_
#define SE_HANDLER_DICT_DICT_STRUCT_H_

#include "field_types.h"
#include "thr_lock.h"
#include "se_utils.h"

namespace smartengine {

// keys on creating
struct Added_key_info
{
  Added_key_info(TABLE *table)
    : altered_table(table), step(TABLE_CREATE), status(HA_EXIT_SUCCESS), dup_key_saved(false) {}
  Added_key_info(const Added_key_info &r)
    : altered_table(r.altered_table), step(r.step), status(r.status.load()) {}
  TABLE *altered_table;

  enum ADD_KEY_STEP{
    /** table create point */
    TABLE_CREATE,

    /** start build index point */
    BUILDING_BASE_INDEX,

    /** build index base finished, and start to do unique check */
    CHECK_UNIQUE_CONSTRAINT,

    /** build index finished */
    FINISHED
  } step;

  std::atomic<uint> status;

  // Mark the dup_key dup_val has been saved.
  std::atomic<bool> dup_key_saved;
};

/** Flags indicating if current operation can be done instantly */
enum class Instant_Type : uint16_t
{
  /** Impossible to alter instantly */
  INSTANT_IMPOSSIBLE,

  /** Can be instant without any change */
  INSTANT_NO_CHANGE,

  /** ADD COLUMN which can be done instantly, including
  adding stored column only (or along with adding virtual columns) */
  INSTANT_ADD_COLUMN
};

struct InstantDDLInfo
{
  InstantDDLInfo() {
    clearup();
  }
  void clearup() {
    m_instant_cols = std::numeric_limits<uint32_t>::max();
    m_null_bytes = std::numeric_limits<uint32_t>::max();
    instantly_added_default_value_null.clear();
    instantly_added_default_values.clear();
  }
  bool have_instantly_added_columns() {
    return !(m_instant_cols == std::numeric_limits<uint32_t>::max() || 
             m_instant_cols == 0);
  }
  uint32_t m_instant_cols = std::numeric_limits<uint32_t>::max();
  uint32_t m_null_bytes = std::numeric_limits<uint32_t>::max();
  std::vector<bool> instantly_added_default_value_null;
  std::vector<std::string> instantly_added_default_values;
};

/*
  Descriptor telling how to decode/encode a field to on-disk record storage
  format. Not all information is in the structure yet, but eventually we
  want to have as much as possible there to avoid virtual calls.

  For encoding/decoding of index tuples, see SeKeyDef.
  */
class SeFieldEncoder
{
public:
  SeFieldEncoder(const SeFieldEncoder &) = delete;
  SeFieldEncoder &operator=(const SeFieldEncoder &) = delete;
  /*
    STORE_NONE is set when a column can be decoded solely from their
    mem-comparable form.
    STORE_SOME is set when a column can be decoded from their mem-comparable
    form plus unpack_info.
    STORE_ALL is set when a column cannot be decoded, so its original value
    must be stored in the PK records.
    */
  enum STORAGE_TYPE {
    STORE_NONE,
    STORE_SOME,
    STORE_ALL,
  };
  STORAGE_TYPE m_storage_type;

  uint m_null_offset;
  uint16 m_field_index;

  uchar m_null_mask; // 0 means the field cannot be null

  my_core::enum_field_types m_field_type;

  uint m_pack_length_in_rec;

  bool maybe_null() const { return m_null_mask != 0; }

  bool uses_variable_len_encoding() const {
    return (m_field_type == MYSQL_TYPE_JSON ||
            m_field_type == MYSQL_TYPE_BLOB ||
            m_field_type == MYSQL_TYPE_VARCHAR);
  }
};

/* Describes instructions on how to decode the field */
class READ_FIELD
{
public:
  /* Points to SeFieldEncoder describing the field */
  SeFieldEncoder *m_field_enc;
  /* if true, decode the field, otherwise skip it */
  bool m_decode;
  /* Skip this many bytes before reading (or skipping) this field */
  int m_skip;
};

/**
  @brief
  SeTableHandler is a reference-counted structure storing information for
  each open table. All the objects are stored in a global hash map.

  //TODO: join this with SeTableDef ?
*/
struct SeTableHandler {
  char *m_table_name;
  uint m_table_name_length;
  int m_ref_count;

  my_core::THR_LOCK m_thr_lock; ///< MySQL latch needed by m_db_lock
};

struct SeOpenTablesMap
{
  /* Hash table used to track the handlers of open tables */
  //my_core::HASH m_hash;
  std::unordered_map<std::string, SeTableHandler *> m_hash;
  /* The mutex used to protect the hash table */
  mutable mysql_mutex_t m_mutex;

  void free_hash(void) { m_hash.clear(); }

  SeTableHandler *get_table_handler(const char *const table_name);

  void release_table_handler(SeTableHandler *const table_handler);

  std::vector<std::string> get_table_names() const;
};


} //namespace smartengine
#endif
