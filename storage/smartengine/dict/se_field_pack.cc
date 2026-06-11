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

#include "se_field_pack.h"
#include "field.h"
#include "field_types.h"
#include "sql_class.h"
#include "se_charset_info.h"
#include "se_field_make_unpack_info.h"
#include "se_field_skip_func.h"
#include "se_field_pack_func.h"
#include "se_field_unpack_func.h"

namespace smartengine {

/*
  (SE_ESCAPE_LENGTH-1) must be an even number so that pieces of lines are not
  split in the middle of an UTF-8 character. See the implementation of
  se_unpack_binary_or_utf8_varchar.
*/
const uint SE_UTF8MB4_LENGTH= 21; //for utf8mb4_general_ci, unpack_info use 21 bits.
const uint SE_ESCAPE_LENGTH = 9;
static_assert((SE_ESCAPE_LENGTH - 1) % 2 == 0,
              "SE_ESCAPE_LENGTH-1 must be even.");

const int VARCHAR_CMP_LESS_THAN_SPACES = 1;
const int VARCHAR_CMP_EQUAL_TO_SPACES = 2;
const int VARCHAR_CMP_GREATER_THAN_SPACES = 3;

const uint SE_TRIMMED_CHARS_OFFSET = 8;

/*
  @brief
    Setup packing of index field into its mem-comparable form

  @detail
    - It is possible produce mem-comparable form for any datatype.
    - Some datatypes also allow to unpack the original value from its
      mem-comparable form.
      = Some of these require extra information to be stored in "unpack_info".
        unpack_info is not a part of mem-comparable form, it is only used to
        restore the original value

  @param
    field  IN  field to be packed/un-packed

  @return
    TRUE  -  Field can be read with index-only reads
    FALSE -  Otherwise
*/

bool SeFieldPacking::setup(const SeKeyDef *const key_descr,
                           const Field *const field,
                           const uint &keynr_arg,
                           const uint &key_part_arg,
                           const uint16 &key_length)
{
  int res = false;
  enum_field_types type = field ? field->real_type() : MYSQL_TYPE_LONGLONG;

  m_keynr = keynr_arg;
  m_key_part = key_part_arg;

  m_maybe_null = field ? field->is_nullable() : false;
  m_unpack_func = nullptr;
  m_make_unpack_info_func = nullptr;
  m_unpack_data_len = 0;
  space_xfrm = nullptr; // safety
  m_weights = 0;  // only used for varchar/char
  m_prefix_index_flag = false;

  /* Calculate image length. By default, is is pack_length() */
  m_max_image_len =
      field ? field->pack_length() : SE_SIZEOF_HIDDEN_PK_COLUMN;

  m_skip_func = se_skip_max_length;
  m_pack_func = se_pack_with_make_sort_key;

  switch (type) {
  case MYSQL_TYPE_LONGLONG:
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_TINY:
    m_unpack_func = se_unpack_integer;
    return true;

  case MYSQL_TYPE_DOUBLE:
    m_pack_func = se_pack_double;
    m_unpack_func = se_unpack_double;
    return true;

  case MYSQL_TYPE_FLOAT:
    m_pack_func = se_pack_float;
    m_unpack_func = se_unpack_float;
    return true;

  case MYSQL_TYPE_NEWDECIMAL:
  /*
    Decimal is packed with Field_new_decimal::make_sort_key, which just
    does memcpy.
    Unpacking decimal values was supported only after fix for issue#253,
    because of that ha_smartengine::get_storage_type() handles decimal values
    in a special way.
  */
  case MYSQL_TYPE_DATETIME2:
  case MYSQL_TYPE_TIMESTAMP2:
  /* These are packed with Field_temporal_with_date_and_timef::make_sort_key */
  case MYSQL_TYPE_TIME2: /* TIME is packed with Field_timef::make_sort_key */
  case MYSQL_TYPE_YEAR:  /* YEAR is packed with  Field_tiny::make_sort_key */
    /* Everything that comes here is packed with just a memcpy(). */
    m_unpack_func = se_unpack_binary_str;
    return true;

  case MYSQL_TYPE_NEWDATE:
    /*
      This is packed by Field_newdate::make_sort_key. It assumes the data is
      3 bytes, and packing is done by swapping the byte order (for both big-
      and little-endian)
    */
    m_unpack_func = se_unpack_newdate;
    return true;
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_JSON:
  case MYSQL_TYPE_BLOB: {
    m_pack_func = se_pack_blob;
    if (key_descr) {
      const CHARSET_INFO *cs = field->charset();
      if (cs == &my_charset_bin) {  //for blob type
        // The my_charset_bin collation is special in that it will consider
        // shorter strings sorting as less than longer strings.
        //
        // See Field_blob::make_sort_key for details.
        m_max_image_len =
            key_length + (field->charset() == &my_charset_bin
                              ? reinterpret_cast<const Field_blob *>(field)
                                    ->pack_length_no_ptr()
                              : 0);

      } else { // for text type
        if (cs == &my_charset_utf8mb4_0900_ai_ci ||
            cs == &my_charset_utf8mb4_general_ci ||
            cs == &my_charset_utf8mb4_bin) {
          // m_weights is number of characters, key_length is length in bytes
          // for utf8mb4_0900_ai_ci for diffent weight length,from 2bytes to 32
          // bytes, only m_max_image_len is not enougth.
          m_weights = key_length > 0 ? key_length / 4 : 0;
        } else if (cs == &my_charset_utf8mb3_general_ci ||
                   cs == &my_charset_utf8mb3_bin) {
          m_weights = key_length > 0 ? key_length / 3 : 0;
        } else if (cs == &my_charset_gbk_chinese_ci ||
                   cs == &my_charset_gbk_bin) {
          m_weights = key_length > 0 ? key_length / 2 : 0;
        } else if (cs == &my_charset_latin1_bin) {
          m_weights = key_length > 0 ? key_length : 0;
        } else {
          // other collation is not supported by se-index, later will report error by create_cfs
        }

        m_max_image_len = cs->coll->strnxfrmlen(cs, key_length);
      }
      // Return false because indexes on text/blob will always require
      // a prefix. With a prefix, the optimizer will not be able to do an
      // index-only scan since there may be content occuring after the prefix
      // length.
      m_prefix_index_flag = true;
      return false;
    }
  }
  default:
    break;
  }

  m_unpack_info_stores_value = false;
  /* Handle [VAR](CHAR|BINARY) */

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING) {
    /*
      For CHAR-based columns, check how strxfrm image will take.
      field->field_length = field->char_length() * cs->mbmaxlen.
    */
    const CHARSET_INFO *cs = field->charset();
    m_max_image_len = cs->coll->strnxfrmlen(cs, field->field_length);
    m_max_strnxfrm_len = m_max_image_len;
    m_charset = cs;
    m_weights = (const_cast<Field*>(field))->char_length();
  }
  const bool is_varchar = (type == MYSQL_TYPE_VARCHAR);
  const CHARSET_INFO *cs = field->charset();
  // max_image_len before chunking is taken into account
  const int max_image_len_before_chunks = m_max_image_len;

  if (is_varchar) {
    // The default for varchar is variable-length, without space-padding for
    // comparisons
    m_skip_func = se_skip_variable_length;
    m_pack_func = se_pack_with_varchar_encoding;
    m_max_image_len =
        (m_max_image_len / (SE_ESCAPE_LENGTH - 1) + 1) * SE_ESCAPE_LENGTH;

    const auto field_var = static_cast<const Field_varstring *>(field);
    m_unpack_info_uses_two_bytes = (field_var->field_length + 8 >= 0x100);
  }

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING) {
    // See http://dev.mysql.com/doc/refman/5.7/en/string-types.html for
    // information about character-based datatypes are compared.
    bool use_unknown_collation = false;
    DBUG_EXECUTE_IF("myx_enable_unknown_collation_index_only_scans",
                    use_unknown_collation = true;);

    if (cs == &my_charset_bin) {
      // - SQL layer pads BINARY(N) so that it always is N bytes long.
      // - For VARBINARY(N), values may have different lengths, so we're using
      //   variable-length encoding. This is also the only charset where the
      //   values are not space-padded for comparison.
      m_unpack_func = is_varchar ? se_unpack_binary_or_utf8_varchar
                                 : se_unpack_binary_str;
      res = true;
    } else if (cs == &my_charset_latin1_bin || cs == &my_charset_utf8mb3_bin ||
               cs == &my_charset_gbk_bin || cs == &my_charset_utf8mb4_bin) {
      // For _bin collations, mem-comparable form of the string is the string
      // itself.
      if (is_varchar) {
        // VARCHARs
        // - are compared as if they were space-padded
        // - but are not actually space-padded (reading the value back
        //   produces the original value, without the padding)
        m_unpack_func = se_unpack_binary_varchar_space_pad;
        m_skip_func = se_skip_variable_space_pad;
        m_pack_func = se_pack_with_varchar_space_pad;
        m_make_unpack_info_func = se_dummy_make_unpack_info;
        m_segment_size = get_segment_size_from_collation(cs);
        m_max_image_len =
            (max_image_len_before_chunks / (m_segment_size - 1) + 1) *
            m_segment_size;
        se_get_mem_comparable_space(cs, &space_xfrm, &space_xfrm_len,
                                     &space_mb_len);
      } else {
        if (cs == &my_charset_gbk_bin) {
          m_pack_func = se_pack_with_make_sort_key_gbk;
        }
        // SQL layer pads CHAR(N) values to their maximum length.
        // We just store that and restore it back.
        m_unpack_func= (cs == &my_charset_latin1_bin)? se_unpack_binary_str:
                                 se_unpack_bin_str;
      }

      res = true;
    } else {
      // This is [VAR]CHAR(n) and the collation is not $(charset_name)_bin

      res = true; // index-only scans are possible
      m_unpack_data_len = is_varchar ? 0 : field->field_length;
      const uint idx = is_varchar ? 0 : 1;
      const SeCollationCodec *codec = nullptr;

      if (is_varchar) {
        // VARCHAR requires space-padding for doing comparisons
        //
        // The check for cs->levels_for_order is to catch
        // latin2_czech_cs and cp1250_czech_cs - multi-level collations
        // that Variable-Length Space Padded Encoding can't handle.
        // It is not expected to work for any other multi-level collations,
        // either.
        // 8.0 removes levels_for_order but leaves levels_for_compare which
        // seems to be identical in value and extremely similar in
        // purpose/indication for our needs here.
        if (cs->levels_for_compare != 1)
        {
          //  NO_LINT_DEBUG
          sql_print_warning("SE: you're trying to create an index "
                            "with a multi-level collation %s", cs->m_coll_name);
          //  NO_LINT_DEBUG
          sql_print_warning("SE will handle this collation internally "
                            " as if it had a NO_PAD attribute.");

          m_pack_func = se_pack_with_varchar_encoding;
          m_skip_func = se_skip_variable_length;
        }
        else if (cs->pad_attribute == PAD_SPACE)
        {
          m_pack_func= se_pack_with_varchar_space_pad;
          m_skip_func= se_skip_variable_space_pad;
          m_segment_size= get_segment_size_from_collation(cs);
          m_max_image_len=
              (max_image_len_before_chunks/(m_segment_size-1) + 1) *
              m_segment_size;
          se_get_mem_comparable_space(cs, &space_xfrm, &space_xfrm_len,
                                       &space_mb_len);
        }
        else if (cs->pad_attribute == NO_PAD) //utf8mb4_0900_ai_ci
        {
          m_pack_func= se_pack_with_varchar_encoding;
          m_skip_func= se_skip_variable_length;
          m_segment_size= get_segment_size_from_collation(cs);
          m_max_image_len=
              (max_image_len_before_chunks/(m_segment_size-1) + 1) *
              m_segment_size;
          res = false;  //now, we just not support index-only-scan for collation utf8mb4_0900_ai_ci
        }
      }
      else //string CHAR[N]
      {
        if (cs == &my_charset_gbk_chinese_ci)
        {
          m_pack_func= se_pack_with_make_sort_key_gbk;
        } else if (cs == &my_charset_utf8mb4_0900_ai_ci) {
          m_pack_func = se_pack_with_make_sort_key_utf8mb4_0900;
        }
      }

      if ((codec = se_init_collation_mapping(cs)) != nullptr) {
        // The collation allows to store extra information in the unpack_info
        // which can be used to restore the original value from the
        // mem-comparable form.
        m_make_unpack_info_func = codec->m_make_unpack_info_func[idx];
        m_unpack_func = codec->m_unpack_func[idx];
        m_charset_codec = codec;
      } else if (cs == &my_charset_utf8mb4_0900_ai_ci) {
        //now, we only support value->mem-cmpkey form, but don't know how to
        //retore value from mem-comparable form.
        assert(m_unpack_func == nullptr);
        assert(m_make_unpack_info_func == nullptr);
        m_unpack_info_stores_value = false;
        res = false;  // Indicate that index-only reads are not possible
      } else if (use_unknown_collation) {
        // We have no clue about how this collation produces mem-comparable
        // form. Our way of restoring the original value is to keep a copy of
        // the original value in unpack_info.
        m_unpack_info_stores_value = true;
        m_make_unpack_info_func = is_varchar ? se_make_unpack_unknown_varchar
                                             : se_make_unpack_unknown;
        m_unpack_func =
            is_varchar ? se_unpack_unknown_varchar : se_unpack_unknown;
      } else {
        // Same as above: we don't know how to restore the value from its
        // mem-comparable form.
        // Here, we just indicate to the SQL layer we can't do it.
        assert(m_unpack_func == nullptr);
        m_unpack_info_stores_value = false;
        res = false; // Indicate that index-only reads are not possible
      }
    }

    // Make an adjustment: unpacking partially covered columns is not
    // possible. field->table is populated when called through
    // SeKeyDef::setup, but not during ha_smartengine::index_flags.
    if (field->table) {
      // Get the original Field object and compare lengths. If this key part is
      // a prefix of a column, then we can't do index-only scans.
      if (field->table->field[field->field_index()]->field_length != key_length) {
        m_unpack_func = nullptr;
        m_make_unpack_info_func = nullptr;
        m_unpack_info_stores_value = true;
        m_prefix_index_flag = true;
        res = false;
      }
    } else {
      if (field->field_length != key_length) {
        m_unpack_func = nullptr;
        m_make_unpack_info_func = nullptr;
        m_unpack_info_stores_value = true;
        res = false;
      }
    }
  }
  return res;
}

/*
  get Field from table, if online-inplace-norebuild ddl, new key is only in the
  altered_table, so we need get field index from altered_table->key_info.

*/
uint SeFieldPacking::get_field_index_in_table(const TABLE *const altered_table) const
{
  KEY *key_info = &altered_table->key_info[m_keynr];

  Field *field = key_info->key_part[m_key_part].field;
  assert(field != nullptr);

  return field->field_index();
}

Field *SeFieldPacking::get_field_in_table(const TABLE *const tbl) const
{
  return tbl->key_info[m_keynr].key_part[m_key_part].field;
}

void SeFieldPacking::fill_hidden_pk_val(uchar **dst, const longlong &hidden_pk_id) const
{
  assert(m_max_image_len == 8);

  String to;
  se_netstr_append_uint64(&to, hidden_pk_id);
  memcpy(*dst, to.ptr(), m_max_image_len);

  *dst += m_max_image_len;
}

} //namespace smartengine

