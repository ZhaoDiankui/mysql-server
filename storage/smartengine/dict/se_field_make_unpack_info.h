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
#ifndef SE_DICT_XE_FIELD_MAKE_UNPACK_INFO_H_
#define SE_DICT_XE_FIELD_MAKE_UNPACK_INFO_H_

#include "my_inttypes.h"

class Field;

namespace smartengine {

struct SeCollationCodec;
class SePackFieldContext;
class SeBitWriter;


/*
  Function of type se_make_unpack_info_t
*/

void se_make_unpack_unknown(const SeCollationCodec *codec,
                            Field *const field,
                            SePackFieldContext *const pack_ctx);

/*
  This point of this function is only to indicate that unpack_info is
  available.

  The actual unpack_info data is produced by the function that packs the key,
  that is, se_pack_with_varchar_space_pad.
*/

void se_dummy_make_unpack_info(const SeCollationCodec *codec,
                               Field *field,
                               SePackFieldContext *pack_ctx);

void se_make_unpack_unknown_varchar(const SeCollationCodec *const codec,
                                    Field *const field,
                                    SePackFieldContext *const pack_ctx);

/*
  Write unpack_data for a "simple" collation
*/
void se_write_unpack_simple(SeBitWriter *const writer,
                            const SeCollationCodec *const codec,
                            const uchar *const src,
                            const size_t src_len);

/*
  Function of type se_make_unpack_info_t

  @detail
    Make unpack_data for VARCHAR(n) in a "simple" charset.
*/

void se_make_unpack_simple_varchar(const SeCollationCodec *const codec,
                                   Field * field,
                                   SePackFieldContext *const pack_ctx);

/*
  Function of type se_make_unpack_info_t

  @detail
    Make unpack_data for CHAR(n) value in a "simple" charset.
    It is CHAR(N), so SQL layer has padded the value with spaces up to N chars.

  @seealso
    The VARCHAR variant is in se_make_unpack_simple_varchar
*/

void se_make_unpack_simple(const SeCollationCodec *const codec,
                           Field *const field,
                           SePackFieldContext *const pack_ctx);

/*
  Write unpack_data for a utf8 collation
*/
void se_write_unpack_utf8(SeBitWriter *writer,
                          const SeCollationCodec *codec,
                          const uchar *src,
                          size_t src_len,
                          bool is_varchar);

/*
  Write unpack_data for a utf8mb4 collation
*/
void se_write_unpack_utf8mb4(SeBitWriter *writer,
                             const SeCollationCodec *codec,
                             const uchar *src,
                             size_t src_len,
                             bool is_varchar);

/*
  Write unpack_data for a gbk collation
*/
void se_write_unpack_gbk(SeBitWriter *writer,
                         const SeCollationCodec *codec,
                         const uchar *src,
                         size_t src_len,
                         bool is_varchar);

/*
  Function of type se_make_unpack_info_t

  @detail
    Make unpack_data for VARCHAR(n) in a specific charset.
*/

void se_make_unpack_varchar(const SeCollationCodec* codec,
                            Field *field,
                            SePackFieldContext *pack_ctx);

/*
  Function of type se_make_unpack_info_t

  @detail
    Make unpack_data for CHAR(n) value in a specific charset.
    It is CHAR(N), so SQL layer has padded the value with spaces up to N chars.

  @seealso
    The VARCHAR variant is in se_make_unpack_varchar
*/

void se_make_unpack_char(const SeCollationCodec *codec,
                         Field *field,
                         SePackFieldContext *pack_ctx);

} //namespace smartengine

#endif
