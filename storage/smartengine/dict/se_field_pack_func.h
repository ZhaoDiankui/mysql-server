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
#ifndef SE_DICT_XE_FIELD_PACK_FUNC_H_
#define SE_DICT_XE_FIELD_PACK_FUNC_H_

#include <vector>
#include "my_inttypes.h"

class Field;

namespace smartengine
{

class SeFieldPacking;
class SePackFieldContext;

/*
 * From Field_blob::make_sort_key in MySQL 5.7
 */
void se_pack_blob(SeFieldPacking *const fpi,
                  Field *const field,
                  uchar *const buf,
                  uchar **dst,
                  SePackFieldContext *const pack_ctx);

void change_double_for_sort(double nr, uchar *to);

void se_pack_double(SeFieldPacking *const fpi,
                    Field *const field,
                    uchar *const buf,
                    uchar **dst,
                    SePackFieldContext *const pack_ctx);

void se_pack_float(SeFieldPacking *const fpi,
                   Field *const field,
                   uchar *const buf,
                   uchar **dst,
                   SePackFieldContext *const pack_ctx);

void se_pack_with_make_sort_key(SeFieldPacking *const fpi,
                                Field *const field,
                                uchar *const buf,
                                uchar **dst,
                                SePackFieldContext *const pack_ctx);


/** Function of type se_index_field_pack_t for utf8mb4_0900_aici MYSQL_TYPE_STRING
 *  for that m_max_image_len is max size of weights, it's not suitable for utf8mb4_0900_ai_ci, every character maybe occupy different bytes for weight.
 *  see also Field_string::make_sort_key
 */
void se_pack_with_make_sort_key_utf8mb4_0900(
    SeFieldPacking *fpi,
    Field *field,
    uchar *buf,
    uchar **dst,
    SePackFieldContext *pack_ctx);

/*
  Function of type se_index_field_pack_t for gbk MYSQL_TYPE_STRING
*/
void se_pack_with_make_sort_key_gbk(SeFieldPacking *fpi,
                                    Field *field,
                                    uchar *buf,
                                    uchar **dst,
                                    SePackFieldContext *pack_ctx);

void se_pack_with_varchar_encoding(SeFieldPacking *const fpi,
                                   Field *const field,
                                   uchar *buf,
                                   uchar **dst,
                                   SePackFieldContext *const pack_ctx);

/*
  Compare the string in [buf..buf_end) with a string that is an infinite
  sequence of strings in space_xfrm
*/

int se_compare_string_with_spaces(const uchar *buf,
                                  const uchar *const buf_end,
                                  const std::vector<uchar> *const space_xfrm);

/*
  Pack the data with Variable-Length Space-Padded Encoding.

  The encoding is there to meet two goals:

  Goal#1. Comparison. The SQL standard says

    " If the collation for the comparison has the PAD SPACE characteristic,
    for the purposes of the comparison, the shorter value is effectively
    extended to the length of the longer by concatenation of <space>s on the
    right.

  At the moment, all MySQL collations except one have the PAD SPACE
  characteristic.  The exception is the "binary" collation that is used by
  [VAR]BINARY columns. (Note that binary collations for specific charsets,
  like utf8_bin or latin1_bin are not the same as "binary" collation, they have
  the PAD SPACE characteristic).

  Goal#2 is to preserve the number of trailing spaces in the original value.

  This is achieved by using the following encoding:
  The key part:
  - Stores mem-comparable image of the column
  - It is stored in chunks of fpi->m_segment_size bytes (*)
    = If the remainder of the chunk is not occupied, it is padded with mem-
      comparable image of the space character (cs->pad_char to be precise).
  - The last byte of the chunk shows how the rest of column's mem-comparable
    image would compare to mem-comparable image of the column extended with
    spaces. There are three possible values.
     - VARCHAR_CMP_LESS_THAN_SPACES,
     - VARCHAR_CMP_EQUAL_TO_SPACES
     - VARCHAR_CMP_GREATER_THAN_SPACES

  VARCHAR_CMP_EQUAL_TO_SPACES means that this chunk is the last one (the rest
  is spaces, or something that sorts as spaces, so there is no reason to store
  it).

  Example: if fpi->m_segment_size=5, and the collation is latin1_bin:

   'abcd\0'   => [ 'abcd' <VARCHAR_CMP_LESS> ]['\0    ' <VARCHAR_CMP_EQUAL> ]
   'abcd'     => [ 'abcd' <VARCHAR_CMP_EQUAL>]
   'abcd   '  => [ 'abcd' <VARCHAR_CMP_EQUAL>]
   'abcdZZZZ' => [ 'abcd' <VARCHAR_CMP_GREATER>][ 'ZZZZ' <VARCHAR_CMP_EQUAL>]

  As mentioned above, the last chunk is padded with mem-comparable images of
  cs->pad_char. It can be 1-byte long (latin1), 2 (utf8_bin), 3 (utf8mb4), etc.

  fpi->m_segment_size depends on the used collation. It is chosen to be such
  that no mem-comparable image of space will ever stretch across the segments
  (see get_segment_size_from_collation).

  == The value part (aka unpack_info) ==
  The value part stores the number of space characters that one needs to add
  when unpacking the string.
  - If the number is positive, it means add this many spaces at the end
  - If the number is negative, it means padding has added extra spaces which
    must be removed.

  Storage considerations
  - depending on column's max size, the number may occupy 1 or 2 bytes
  - the number of spaces that need to be removed is not more than
    SE_TRIMMED_CHARS_OFFSET=8, so we offset the number by that value and
    then store it as unsigned.

  @seealso
    se_unpack_binary_varchar_space_pad
    se_unpack_simple_varchar_space_pad
    se_dummy_make_unpack_info
    se_skip_variable_space_pad
*/

void se_pack_with_varchar_space_pad(SeFieldPacking *fpi,
                                    Field *field,
                                    uchar *buf,
                                    uchar **dst,
                                    SePackFieldContext *pack_ctx);

} //namespace smartengine

#endif
