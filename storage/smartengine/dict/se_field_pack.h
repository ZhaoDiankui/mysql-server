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
#ifndef SE_DICT_XE_FIELD_PACK_H_
#define SE_DICT_XE_FIELD_PACK_H_

#include "dict/se_index.h"

extern CHARSET_INFO my_charset_gbk_chinese_ci;
extern CHARSET_INFO my_charset_gbk_bin;
extern CHARSET_INFO my_charset_utf8mb4_bin;
extern CHARSET_INFO my_charset_utf16_bin;
extern CHARSET_INFO my_charset_utf16le_bin;
extern CHARSET_INFO my_charset_utf32_bin;

namespace smartengine
{

struct SeCollationCodec;

/*
  (SE_ESCAPE_LENGTH-1) must be an even number so that pieces of lines are not
  split in the middle of an UTF-8 character. See the implementation of
  se_unpack_binary_or_utf8_varchar.
*/
extern const uint SE_UTF8MB4_LENGTH; //for utf8mb4_general_ci, unpack_info use 21 bits.
extern const uint SE_ESCAPE_LENGTH;

extern const int VARCHAR_CMP_LESS_THAN_SPACES;
extern const int VARCHAR_CMP_EQUAL_TO_SPACES;
extern const int VARCHAR_CMP_GREATER_THAN_SPACES;

extern const uint SE_TRIMMED_CHARS_OFFSET;
/*
  @brief
  Field packing context.
  The idea is to ensure that a call to se_index_field_pack_t function
  is followed by a call to se_make_unpack_info_t.

  @detail
  For some datatypes, unpack_info is produced as a side effect of
  se_index_field_pack_t function call.
  For other datatypes, packing is just calling make_sort_key(), while
  se_make_unpack_info_t is a custom function.
  In order to accommodate both cases, we require both calls to be made and
  unpack_info is passed as context data between the two.
*/
class SePackFieldContext
{
public:
  SePackFieldContext(const SePackFieldContext &) = delete;
  SePackFieldContext &operator=(const SePackFieldContext &) = delete;

  explicit SePackFieldContext(SeStringWriter *const writer_arg)
      : writer(writer_arg) {}

  // NULL means we're not producing unpack_info.
  SeStringWriter *writer;
};

/*
  C-style "virtual table" allowing different handling of packing logic based
  on the field type. See SeFieldPacking::setup() implementation.
  */
using se_make_unpack_info_t = void (*)(const SeCollationCodec *codec,
                                       Field * field,
                                       SePackFieldContext *const pack_ctx);
using se_index_field_unpack_t = int (*)(SeFieldPacking *fpi,
                                        Field *field,
                                        uchar *field_ptr,
                                        SeStringReader *reader,
                                        SeStringReader *unpack_reader);
using se_index_field_skip_t = int (*)(const SeFieldPacking *fpi,
                                      const Field *field,
                                      SeStringReader *reader);

using se_index_field_pack_t = void (*)(SeFieldPacking *fpi,
                                       Field *field,
                                       uchar *buf,
                                       uchar **dst,
                                       SePackFieldContext *pack_ctx);

class SeFieldPacking
{
public:
  SeFieldPacking(const SeFieldPacking &) = delete;
  SeFieldPacking &operator=(const SeFieldPacking &) = delete;
  SeFieldPacking() = default;

  /* Length of mem-comparable image of the field, in bytes */
  int m_max_image_len;

  /* Length of strnxfrm length of the field, in bytes */
  int m_max_strnxfrm_len;

  /* number of characters be part of key */
  uint m_weights;

  /* prefix_flag*/
  bool m_prefix_index_flag;

  /* Length of image in the unpack data */
  int m_unpack_data_len;
  int m_unpack_data_offset;

  bool m_maybe_null; /* TRUE <=> NULL-byte is stored */

  /*
    Valid only for CHAR/VARCHAR fields.
  */
  const CHARSET_INFO *m_charset;

  // (Valid when Variable Length Space Padded Encoding is used):
  uint m_segment_size; // size of segment used

  // number of bytes used to store number of trimmed (or added)
  // spaces in the upack_info
  bool m_unpack_info_uses_two_bytes;

  const std::vector<uchar> *space_xfrm;
  size_t space_xfrm_len;
  size_t space_mb_len;

  const SeCollationCodec *m_charset_codec;

  /*
    @return TRUE: this field makes use of unpack_info.
  */
  bool uses_unpack_info() const { return (m_make_unpack_info_func != nullptr); }

  /* TRUE means unpack_info stores the original field value */
  bool m_unpack_info_stores_value;

  se_index_field_pack_t m_pack_func;
  se_make_unpack_info_t m_make_unpack_info_func;

  /*
    This function takes
    - mem-comparable form
    - unpack_info data
    and restores the original value.
  */
  se_index_field_unpack_t m_unpack_func;

  /*
    This function skips over mem-comparable form.
  */
  se_index_field_skip_t m_skip_func;

private:
  static const int64_t SE_SIZEOF_HIDDEN_PK_COLUMN = sizeof(longlong);

private:
  /*
    Location of the field in the table (key number and key part number).

    Note that this describes not the field, but rather a position of field in
    the index. Consider an example:

      col1 VARCHAR (100),
      INDEX idx1 (col1)),
      INDEX idx2 (col1(10)),

    Here, idx2 has a special Field object that is set to describe a 10-char
    prefix of col1.

    We must also store the keynr. It is needed for implicit "extended keys".
    Every key in SE needs to include PK columns.  Generally, SQL layer
    includes PK columns as part of its "Extended Keys" feature, but sometimes
    it does not (known examples are unique secondary indexes and partitioned
    tables).
    In that case, SE's index descriptor has invisible suffix of PK
    columns (and the point is that these columns are parts of PK, not parts
    of the current index).
  */
  uint m_keynr;
  uint m_key_part;

public:
  bool setup(const SeKeyDef *const key_descr,
             const Field *const field,
             const uint &keynr_arg,
             const uint &key_part_arg,
             const uint16 &key_length);

  Field *get_field_in_table(const TABLE *const tbl) const;

  uint get_field_index_in_table(const TABLE *const altered_table) const;

  void fill_hidden_pk_val(uchar **dst, const longlong &hidden_pk_id) const;
};

} //namespace smartengine

#endif
