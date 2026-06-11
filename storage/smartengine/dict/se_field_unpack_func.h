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
#ifndef SE_DICT_XE_FIELD_UNPACK_FUNC_H_
#define SE_DICT_XE_FIELD_UNPACK_FUNC_H_

#include "my_inttypes.h"

class Field;
namespace smartengine
{
class SeFieldPacking;
class SeStringReader;
class SeBitReader;
struct SeCollationCodec;

// Possible return values for se_index_field_unpack_t functions.
enum {
  UNPACK_SUCCESS = 0,
  UNPACK_FAILURE = 1,
  UNPACK_INFO_MISSING= 2
};

int se_unpack_integer(SeFieldPacking *const fpi,
                      Field *const field,
                      uchar *const to,
                      SeStringReader *const reader,
                      SeStringReader *const unp_reader);

#if !defined(WORDS_BIGENDIAN)
void se_swap_double_bytes(uchar *const dst, const uchar *const src);

void se_swap_float_bytes(uchar *const dst, const uchar *const src);
#else
#define se_swap_double_bytes nullptr
#define se_swap_float_bytes nullptr
#endif

int se_unpack_floating_point(uchar *const dst,
                             SeStringReader *const reader,
                             const size_t &size,
                             const int &exp_digit,
                             const uchar *const zero_pattern,
                             const uchar *const zero_val,
                             void (*swap_func)(uchar *, const uchar *));

/*
  Function of type se_index_field_unpack_t

  Unpack a double by doing the reverse action of change_double_for_sort
  (sql/filesort.cc).  Note that this only works on IEEE values.
  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
int se_unpack_double(SeFieldPacking *const fpi,
                     Field *const field,
                     uchar *const field_ptr,
                     SeStringReader *const reader,
                     SeStringReader *const unp_reader);

/*
  Function of type se_index_field_unpack_t

  Unpack a float by doing the reverse action of Field_float::make_sort_key
  (sql/field.cc).  Note that this only works on IEEE values.
  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
int se_unpack_float(SeFieldPacking *const,
                    Field *const field,
                    uchar *const field_ptr,
                    SeStringReader *const reader,
                    SeStringReader *const unp_reader);

/*
  Function of type se_index_field_unpack_t used to
  Unpack by doing the reverse action to Field_newdate::make_sort_key.
*/

int se_unpack_newdate(SeFieldPacking *const fpi,
                      Field *constfield,
                      uchar *const field_ptr,
                      SeStringReader *const reader,
                      SeStringReader *const unp_reader);

/*
  Function of type se_index_field_unpack_t, used to
  Unpack the string by copying it over.
  This is for BINARY(n) where the value occupies the whole length.
*/

int se_unpack_binary_str(SeFieldPacking *const fpi,
                         Field *const field,
                         uchar *const to,
                         SeStringReader *const reader,
                         SeStringReader *const unp_reader);

/*
  Function of type se_index_field_unpack_t.
  For UTF-8, we need to convert 2-byte wide-character entities back into
  UTF8 sequences.
*/
int se_unpack_bin_str(SeFieldPacking *fpi,
                      Field *field,
                      uchar *dst,
                      SeStringReader *reader,
                      SeStringReader *unp_reader);

int se_unpack_binary_or_utf8_varchar(SeFieldPacking *fpi,
                                     Field *field,
                                     uchar *dst,
                                     SeStringReader *reader,
                                     SeStringReader *unp_reader);

/*
  @seealso
    se_pack_with_varchar_space_pad - packing function
    se_unpack_simple_varchar_space_pad - unpacking function for 'simple'
    charsets.
    se_skip_variable_space_pad - skip function
*/
int se_unpack_binary_varchar_space_pad(SeFieldPacking *fpi,
                                       Field *field,
                                       uchar *dst,
                                       SeStringReader *reader,
                                       SeStringReader *unp_reader);

int se_unpack_unknown(SeFieldPacking *const fpi,
                      Field *const field,
                      uchar *const dst,
                      SeStringReader *const reader,
                      SeStringReader *const unp_reader);

/*
  Function of type se_index_field_unpack_t

  @detail
  Unpack a key part in an "unknown" collation from its
  (mem_comparable_form, unpack_info) form.

  "Unknown" means we have no clue about how mem_comparable_form is made from
  the original string, so we keep the whole original string in the unpack_info.

  @seealso
    se_make_unpack_unknown, se_unpack_unknown
*/

int se_unpack_unknown_varchar(SeFieldPacking *const fpi,
                              Field *const field,
                              uchar *dst,
                              SeStringReader *const reader,
                              SeStringReader *const unp_reader);

uint se_read_unpack_simple(SeBitReader *reader,
                           const SeCollationCodec *codec,
                           const uchar *src,
                           uchar src_len,
                           uchar *dst);

/*
  Function of type se_index_field_unpack_t

  @seealso
    se_pack_with_varchar_space_pad - packing function
    se_unpack_binary_or_utf8_varchar_space_pad - a similar unpacking function
*/

int se_unpack_simple_varchar_space_pad(SeFieldPacking *const fpi,
                                       Field *const field,
                                       uchar *dst,
                                       SeStringReader *const reader,
                                       SeStringReader *const unp_reader);

int se_unpack_simple(SeFieldPacking *const fpi,
                     Field *const field,
                     uchar *const dst,
                     SeStringReader *const reader,
                     SeStringReader *const unp_reader);

uint se_read_unpack_utf8(SeBitReader *reader,
                         const SeCollationCodec *codec,
                         const uchar *src,
                         size_t src_len,
                         uchar *dst,
                         size_t *dst_len);

uint se_read_unpack_utf8mb4(SeBitReader *reader,
                            const SeCollationCodec *codec,
                            const uchar *src,
                            size_t src_len,
                            uchar *dst,
                            size_t *dst_len);

uint se_read_unpack_gbk(SeBitReader *reader,
                        const SeCollationCodec *codec,
                        const uchar *src,
                        size_t src_len,
                        uchar *dst,
                        size_t *dst_len);

/*
  Function of type se_index_field_unpack_t

  @seealso
    se_pack_with_varchar_space_pad - packing function
    se_unpack_binary_varchar_space_pad - a similar unpacking function
*/

int se_unpack_varchar_space_pad(SeFieldPacking *fpi,
                                Field *field,
                                uchar *dst,
                                SeStringReader *reader,
                                SeStringReader *unp_reader);

int se_unpack_char(SeFieldPacking *fpi,
                   Field *field,
                   uchar *dst,
                   SeStringReader *reader,
                   SeStringReader *unp_reader);

} //namespace smartengine

#endif
