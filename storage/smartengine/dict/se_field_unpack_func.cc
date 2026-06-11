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
#include "se_field_unpack_func.h"
#include "sql/field.h"
#include "dict/se_charset_info.h"
#include "dict/se_field_skip_func.h"
#include "dict/se_field_pack.h"

namespace smartengine
{

int se_unpack_integer(SeFieldPacking *const fpi,
                      Field *const field,
                      uchar *const to,
                      SeStringReader *const reader,
                      SeStringReader *const unp_reader)
{
  const int length = fpi->m_max_image_len;

  const uchar *from;
  if (!(from = (const uchar *)reader->read(length)))
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */

#ifdef WORDS_BIGENDIAN
  {
    if (((Field_num *)field)->unsigned_flag)
      to[0] = from[0];
    else
      to[0] = (char)(from[0] ^ 128); // Reverse the sign bit.
    memcpy(to + 1, from + 1, length - 1);
  }
#else
  {
    const int sign_byte = from[0];
    if (((Field_num *)field)->is_unsigned())
      to[length - 1] = sign_byte;
    else
      to[length - 1] =
          static_cast<char>(sign_byte ^ 128); // Reverse the sign bit.
    for (int i = 0, j = length - 1; i < length - 1; ++i, --j)
      to[i] = from[j];
  }
#endif
  return UNPACK_SUCCESS;
}

#if !defined(WORDS_BIGENDIAN)
void se_swap_double_bytes(uchar *const dst, const uchar *const src)
{
#if defined(__FLOAT_WORD_ORDER) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)
  // A few systems store the most-significant _word_ first on little-endian
  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
  dst[4] = src[7];
  dst[5] = src[6];
  dst[6] = src[5];
  dst[7] = src[4];
#else
  dst[0] = src[7];
  dst[1] = src[6];
  dst[2] = src[5];
  dst[3] = src[4];
  dst[4] = src[3];
  dst[5] = src[2];
  dst[6] = src[1];
  dst[7] = src[0];
#endif
}

void se_swap_float_bytes(uchar *const dst, const uchar *const src)
{
  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
}
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
                             void (*swap_func)(uchar *, const uchar *))
{
  const uchar *const from = (const uchar *)reader->read(size);
  if (from == nullptr)
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */

  /* Check to see if the value is zero */
  if (memcmp(from, zero_pattern, size) == 0) {
    memcpy(dst, zero_val, size);
    return UNPACK_SUCCESS;
  }

#if defined(WORDS_BIGENDIAN)
  // On big-endian, output can go directly into result
  uchar *const tmp = dst;
#else
  // Otherwise use a temporary buffer to make byte-swapping easier later
  uchar tmp[8];
#endif

  memcpy(tmp, from, size);

  if (tmp[0] & 0x80) {
    // If the high bit is set the original value was positive so
    // remove the high bit and subtract one from the exponent.
    ushort exp_part = ((ushort)tmp[0] << 8) | (ushort)tmp[1];
    exp_part &= 0x7FFF;                            // clear high bit;
    exp_part -= (ushort)1 << (16 - 1 - exp_digit); // subtract from exponent
    tmp[0] = (uchar)(exp_part >> 8);
    tmp[1] = (uchar)exp_part;
  } else {
    // Otherwise the original value was negative and all bytes have been
    // negated.
    for (size_t ii = 0; ii < size; ii++)
      tmp[ii] ^= 0xFF;
  }

#if !defined(WORDS_BIGENDIAN)
  // On little-endian, swap the bytes around
  swap_func(dst, tmp);
#else
  static_assert(swap_func == nullptr, "Assuming that no swapping is needed.");
#endif

  return UNPACK_SUCCESS;
}

#if !defined(DBL_EXP_DIG)
#define DBL_EXP_DIG (sizeof(double) * 8 - DBL_MANT_DIG)
#endif

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
                     SeStringReader *const unp_reader)
{
  static double zero_val = 0.0;
  static const uchar zero_pattern[8] = {128, 0, 0, 0, 0, 0, 0, 0};

  return se_unpack_floating_point(
      field_ptr, reader, sizeof(double), DBL_EXP_DIG, zero_pattern,
      (const uchar *)&zero_val, se_swap_double_bytes);
}

#if !defined(FLT_EXP_DIG)
#define FLT_EXP_DIG (sizeof(float) * 8 - FLT_MANT_DIG)
#endif

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
                    SeStringReader *const unp_reader)
{
  static float zero_val = 0.0;
  static const uchar zero_pattern[4] = {128, 0, 0, 0};

  return se_unpack_floating_point(
      field_ptr, reader, sizeof(float), FLT_EXP_DIG, zero_pattern,
      (const uchar *)&zero_val, se_swap_float_bytes);
}

/*
  Function of type se_index_field_unpack_t used to
  Unpack by doing the reverse action to Field_newdate::make_sort_key.
*/

int se_unpack_newdate(SeFieldPacking *const fpi,
                      Field *constfield,
                      uchar *const field_ptr,
                      SeStringReader *const reader,
                      SeStringReader *const unp_reader)
{
  const char *from;
  assert(fpi->m_max_image_len == 3);

  if (!(from = reader->read(3)))
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */

  field_ptr[0] = from[2];
  field_ptr[1] = from[1];
  field_ptr[2] = from[0];
  return UNPACK_SUCCESS;
}

/*
  Function of type se_index_field_unpack_t, used to
  Unpack the string by copying it over.
  This is for BINARY(n) where the value occupies the whole length.
*/
int se_unpack_binary_str(SeFieldPacking *const fpi,
                         Field *const field,
                         uchar *const to,
                         SeStringReader *const reader,
                         SeStringReader *const unp_reader)
{
  const char *from;
  if (!(from = reader->read(fpi->m_max_image_len)))
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */

  memcpy(to, from, fpi->m_max_image_len);
  return UNPACK_SUCCESS;
}

/*
  Function of type se_index_field_unpack_t.
  For UTF-8, we need to convert 2-byte wide-character entities back into
  UTF8 sequences.
*/
int se_unpack_bin_str(SeFieldPacking *fpi,
                      Field *field,
                      uchar *dst,
                      SeStringReader *reader,
                      SeStringReader *unp_reader)
{
  my_core::CHARSET_INFO *cset= (my_core::CHARSET_INFO*)field->charset();
  const uchar *src;
  if (!(src= (const uchar*)reader->read(fpi->m_max_image_len)))
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */

  const uchar *src_end= src + fpi->m_max_image_len;
  uchar *dst_end= dst + field->pack_length();
  int res;

  if (cset == &my_charset_utf8mb3_bin)
  {
    while (src < src_end)
    {
      my_wc_t wc= (src[0] <<8) | src[1];
      src += 2;
      res= cset->cset->wc_mb(cset, wc, dst, dst_end);
      assert(res > 0 && res <=3);
      if (res < 0)
        return UNPACK_FAILURE;
      dst += res;
    }
  }
  else if (cset == &my_charset_gbk_bin)
  {
    while (src < src_end)
    {
      if (src[0] == 0x20)
      {
        /* src[0] is not used */
        dst[0]= src[1];
        res= 1;
      }
      else
      {
        dst[0]= src[0];
        dst[1]= src[1];
        res= 2;
      }

      src += 2;
      dst += res;
    }
  }
  else if (cset == &my_charset_utf8mb4_bin)
  {
    while (src < src_end)
    {
      my_wc_t wc= (src[0] <<16 | src[1] <<8) | src[2];
      src += 3;
      int res= cset->cset->wc_mb(cset, wc, dst, dst_end);
      assert(res > 0 && res <=4);
      if (res < 0)
        return UNPACK_FAILURE;
      dst += res;
    }
  }

  cset->cset->fill(cset, reinterpret_cast<char *>(dst),
                   dst_end - dst, cset->pad_char);
  return UNPACK_SUCCESS;
}

int se_unpack_binary_or_utf8_varchar(SeFieldPacking *fpi,
                                     Field *field,
                                     uchar *dst,
                                     SeStringReader *reader,
                                     SeStringReader *unp_reader)
{
  const uchar *ptr;
  size_t len= 0;
  bool finished= false;
  uchar *d0= dst;
  Field_varstring* field_var= (Field_varstring*)field;
  dst += field_var->get_length_bytes();
  // How much we can unpack
  size_t dst_len= field_var->pack_length() - field_var->get_length_bytes();
  uchar *dst_end= dst + dst_len;

  /* Decode the length-emitted encoding here */
  while ((ptr= (const uchar*)reader->read(SE_ESCAPE_LENGTH)))
  {
    /* See se_pack_with_varchar_encoding. */
    uchar pad= 255 - ptr[SE_ESCAPE_LENGTH - 1];  // number of padding bytes
    uchar used_bytes= SE_ESCAPE_LENGTH - 1 - pad;

    if (used_bytes > SE_ESCAPE_LENGTH - 1)
    {
      return UNPACK_FAILURE; /* cannot store that much, invalid data */
    }

    if (dst_len < used_bytes)
    {
      /* Encoded index tuple is longer than the size in the record buffer? */
      return UNPACK_FAILURE;
    }

    /*
      Now, we need to decode used_bytes of data and append them to the value.
    */
    if (fpi->m_charset == &my_charset_utf8mb3_bin)
    {
      if (used_bytes & 1)
      {
        /*
          UTF-8 characters are encoded into two-byte entities. There is no way
          we can have an odd number of bytes after encoding.
        */
        return UNPACK_FAILURE;
      }

      const uchar *src= ptr;
      const uchar *src_end= ptr + used_bytes;
      while (src < src_end)
      {
        my_wc_t wc= (src[0] <<8) | src[1];
        src += 2;
        const CHARSET_INFO *cset= fpi->m_charset;
        int res= cset->cset->wc_mb(cset, wc, dst, dst_end);
        assert(res > 0 && res <=3);
        if (res < 0)
          return UNPACK_FAILURE;
        dst += res;
        len += res;
        dst_len -= res;
      }
    }
    else
    {
      memcpy(dst, ptr, used_bytes);
      dst += used_bytes;
      dst_len -= used_bytes;
      len += used_bytes;
    }

    if (used_bytes < SE_ESCAPE_LENGTH - 1)
    {
      finished= true;
      break;
    }
  }

  if (!finished)
    return UNPACK_FAILURE;

  /* Save the length */
  if (field_var->get_length_bytes() == 1)
  {
    d0[0]= len;
  }
  else
  {
    assert(field_var->get_length_bytes() == 2);
    int2store(d0, len);
  }
  return UNPACK_SUCCESS;
}

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
                                       SeStringReader *unp_reader)
{
  const uchar *ptr;
  size_t len= 0;
  bool finished= false;
  Field_varstring* field_var= static_cast<Field_varstring *>(field);
  uchar *d0= dst;
  uchar *dst_end= dst + field_var->pack_length();
  dst += field_var->get_length_bytes();

  uint space_padding_bytes= 0;
  uint extra_spaces;
  if (!unp_reader)
  {
    return UNPACK_INFO_MISSING;
  }

  if ((fpi->m_unpack_info_uses_two_bytes?
       unp_reader->read_uint16(&extra_spaces):
       unp_reader->read_uint8(&extra_spaces)))
  {
    return UNPACK_FAILURE;
  }

  if (extra_spaces <= SE_TRIMMED_CHARS_OFFSET)
  {
    space_padding_bytes= -(static_cast<int>(extra_spaces) -
                           SE_TRIMMED_CHARS_OFFSET);
    extra_spaces= 0;
  }
  else
    extra_spaces -= SE_TRIMMED_CHARS_OFFSET;

  space_padding_bytes *= fpi->space_xfrm_len;

  /* Decode the length-emitted encoding here */
  while ((ptr= (const uchar*)reader->read(fpi->m_segment_size)))
  {
    char last_byte= ptr[fpi->m_segment_size - 1];
    size_t used_bytes;
    if (last_byte == VARCHAR_CMP_EQUAL_TO_SPACES)  // this is the last segment
    {
      if (space_padding_bytes > (fpi->m_segment_size-1))
        return UNPACK_FAILURE;  // Cannot happen, corrupted data
      used_bytes= (fpi->m_segment_size-1) - space_padding_bytes;
      finished= true;
    }
    else
    {
      if (last_byte != VARCHAR_CMP_LESS_THAN_SPACES &&
          last_byte != VARCHAR_CMP_GREATER_THAN_SPACES)
      {
        return UNPACK_FAILURE;  // Invalid value
      }
      used_bytes= fpi->m_segment_size-1;
    }

    // Now, need to decode used_bytes of data and append them to the value.
    if (fpi->m_charset == &my_charset_utf8mb3_bin)
    {
      if (used_bytes & 1)
      {
        /*
          UTF-8 characters are encoded into two-byte entities. There is no way
          we can have an odd number of bytes after encoding.
        */
        return UNPACK_FAILURE;
      }

      const uchar *src= ptr;
      const uchar *src_end= ptr + used_bytes;
      while (src < src_end)
      {
        my_wc_t wc= (src[0] <<8) | src[1];
        src += 2;
        const CHARSET_INFO *cset= fpi->m_charset;
        int res= cset->cset->wc_mb(cset, wc, dst, dst_end);
        assert(res <=3);
        if (res <= 0)
          return UNPACK_FAILURE;
        dst += res;
        len += res;
      }
    }
    else if (fpi->m_charset == &my_charset_gbk_bin)
    {
      if (used_bytes & 1)
      {
        /*
          GBK characters are encoded into two-byte entities. There is no way
          we can have an odd number of bytes after encoding.
        */
        return UNPACK_FAILURE;
      }

      const uchar *src= ptr;
      const uchar *src_end= ptr + used_bytes;
      int res;
      while (src < src_end)
      {
        if (src[0] == 0x20)
        {
          /* src[0] is not used */
          dst[0]= src[1];
          res= 1;
        }
        else
        {
          dst[0]= src[0];
          dst[1]= src[1];
          res= 2;
        }

        src += 2;
        dst += res;
        len += res;
      }
    }
    else if (fpi->m_charset == &my_charset_utf8mb4_bin)
    {
      if ((used_bytes % 3) != 0)
      {
        /*
          UTF-8 characters are encoded into three-byte entities. There is no way
          we can an odd number of bytes after encoding.
        */
        return UNPACK_FAILURE;
      }

      const uchar *src= ptr;
      const uchar *src_end= ptr + used_bytes;
      while (src < src_end)
      {
        my_wc_t wc= (src[0] <<16) | src[1] <<8 | src[2];
        src += 3;
        const CHARSET_INFO *cset= fpi->m_charset;
        int res= cset->cset->wc_mb(cset, wc, dst, dst_end);
        assert(res > 0 && res <=4);
        if (res < 0)
          return UNPACK_FAILURE;
        dst += res;
        len += res;
      }
    }
    else
    {
      if (dst + used_bytes > dst_end)
        return UNPACK_FAILURE;
      memcpy(dst, ptr, used_bytes);
      dst += used_bytes;
      len += used_bytes;
    }

    if (finished)
    {
      if (extra_spaces)
      {
        // Both binary and UTF-8 charset store space as ' ',
        // so the following is ok:
        if (dst + extra_spaces > dst_end)
          return UNPACK_FAILURE;
        memset(dst, fpi->m_charset->pad_char, extra_spaces);
        len += extra_spaces;
      }
      break;
    }
  }

  if (!finished)
    return UNPACK_FAILURE;

  /* Save the length */
  if (field_var->get_length_bytes() == 1)
  {
    d0[0]= len;
  }
  else
  {
    assert(field_var->get_length_bytes() == 2);
    int2store(d0, len);
  }
  return UNPACK_SUCCESS;
}

int se_unpack_unknown(SeFieldPacking *const fpi,
                      Field *const field,
                      uchar *const dst,
                      SeStringReader *const reader,
                      SeStringReader *const unp_reader)
{
  const uchar *ptr;
  const uint len = fpi->m_unpack_data_len;
  // We don't use anything from the key, so skip over it.
  if (se_skip_max_length(fpi, field, reader)) {
    return UNPACK_FAILURE;
  }

  DBUG_ASSERT_IMP(len > 0, unp_reader != nullptr);

  if ((ptr = (const uchar *)unp_reader->read(len))) {
    memcpy(dst, ptr, len);
    return UNPACK_SUCCESS;
  }
  return UNPACK_FAILURE;
}

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
                              SeStringReader *const unp_reader)
{
  const uchar *ptr;
  uchar *const d0 = dst;
  const auto f = static_cast<Field_varstring *>(field);
  dst += f->get_length_bytes();
  const uint len_bytes = f->get_length_bytes();
  // We don't use anything from the key, so skip over it.
  if (fpi->m_skip_func(fpi, field, reader)) {
    return UNPACK_FAILURE;
  }

  assert(len_bytes > 0);
  assert(unp_reader != nullptr);

  if ((ptr = (const uchar *)unp_reader->read(len_bytes))) {
    memcpy(d0, ptr, len_bytes);
    const uint len = len_bytes == 1 ? (uint)*ptr : uint2korr(ptr);
    if ((ptr = (const uchar *)unp_reader->read(len))) {
      memcpy(dst, ptr, len);
      return UNPACK_SUCCESS;
    }
  }
  return UNPACK_FAILURE;
}

uint se_read_unpack_simple(SeBitReader *reader,
                           const SeCollationCodec *codec,
                           const uchar *src,
                           uchar src_len,
                           uchar *dst)
{
  const auto c= reinterpret_cast<const Se_collation_codec_simple*>(codec);
  for (uint i= 0; i < src_len; i++)
  {
    if (c->m_dec_size[src[i]] > 0)
    {
      uint *ret;
      // Unpack info is needed but none available.
      if (reader == nullptr)
      {
        return UNPACK_INFO_MISSING;
      }

      if ((ret= reader->read(c->m_dec_size[src[i]])) == nullptr)
      {
        return UNPACK_FAILURE;
      }
      dst[i]= c->m_dec_idx[*ret][src[i]];
    }
    else
    {
      dst[i]= c->m_dec_idx[0][src[i]];
    }
  }

  return UNPACK_SUCCESS;
}

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
                                       SeStringReader *const unp_reader)
{
  const uchar *ptr;
  size_t len = 0;
  bool finished = false;
  uchar *d0 = dst;
  Field_varstring *const field_var =
      static_cast<Field_varstring *>(field);
  // For simple collations, char_length is also number of bytes.
  assert((size_t)fpi->m_max_image_len >= field_var->char_length());
  uchar *dst_end = dst + field_var->pack_length();
  dst += field_var->get_length_bytes();
  SeBitReader bit_reader(unp_reader);

  uint space_padding_bytes = 0;
  uint extra_spaces;
  assert(unp_reader != nullptr);

  if ((fpi->m_unpack_info_uses_two_bytes
           ? unp_reader->read_uint16(&extra_spaces)
           : unp_reader->read_uint8(&extra_spaces))) {
    return UNPACK_FAILURE;
  }

  if (extra_spaces <= 8) {
    space_padding_bytes = -(static_cast<int>(extra_spaces) - 8);
    extra_spaces = 0;
  } else
    extra_spaces -= 8;

  space_padding_bytes *= fpi->space_xfrm_len;

  /* Decode the length-emitted encoding here */
  while ((ptr = (const uchar *)reader->read(fpi->m_segment_size))) {
    const char last_byte =
        ptr[fpi->m_segment_size - 1]; // number of padding bytes
    size_t used_bytes;
    if (last_byte == VARCHAR_CMP_EQUAL_TO_SPACES) {
      // this is the last one
      if (space_padding_bytes > (fpi->m_segment_size - 1))
        return UNPACK_FAILURE; // Cannot happen, corrupted data
      used_bytes = (fpi->m_segment_size - 1) - space_padding_bytes;
      finished = true;
    } else {
      if (last_byte != VARCHAR_CMP_LESS_THAN_SPACES &&
          last_byte != VARCHAR_CMP_GREATER_THAN_SPACES) {
        return UNPACK_FAILURE;
      }
      used_bytes = fpi->m_segment_size - 1;
    }

    if (dst + used_bytes > dst_end) {
      // The value on disk is longer than the field definition allows?
      return UNPACK_FAILURE;
    }

    uint ret;
    if ((ret = se_read_unpack_simple(&bit_reader, fpi->m_charset_codec, ptr,
                                      used_bytes, dst)) != UNPACK_SUCCESS) {
      return ret;
    }

    dst += used_bytes;
    len += used_bytes;

    if (finished) {
      if (extra_spaces) {
        if (dst + extra_spaces > dst_end)
          return UNPACK_FAILURE;
        // pad_char has a 1-byte form in all charsets that
        // are handled by se_init_collation_mapping.
        memset(dst, field_var->charset()->pad_char, extra_spaces);
        len += extra_spaces;
      }
      break;
    }
  }

  if (!finished)
    return UNPACK_FAILURE;

  /* Save the length */
  if (field_var->get_length_bytes() == 1) {
    d0[0] = len;
  } else {
    assert(field_var->get_length_bytes() == 2);
    int2store(d0, len);
  }
  return UNPACK_SUCCESS;
}

int se_unpack_simple(SeFieldPacking *const fpi,
                     Field *const field,
                     uchar *const dst,
                     SeStringReader *const reader,
                     SeStringReader *const unp_reader)
{
  const uchar *ptr;
  const uint len = fpi->m_max_image_len;
  SeBitReader bit_reader(unp_reader);

  if (!(ptr = (const uchar *)reader->read(len))) {
    return UNPACK_FAILURE;
  }

  return se_read_unpack_simple(unp_reader ? &bit_reader : nullptr,
      fpi->m_charset_codec, ptr, len, dst);
}

uint se_read_unpack_utf8(SeBitReader *reader,
                         const SeCollationCodec *codec,
                         const uchar *src,
                         size_t src_len,
                         uchar *dst,
                         size_t *dst_len)
{
  const auto c= static_cast<const Se_collation_codec_utf8*>(codec);
  int len= 0;
  assert(src_len % 2 == 0);
  for (uint i= 0; i < src_len; i+=2)
  {
    my_wc_t src_char, dst_char;
    src_char = src[i] << 8 | src[i+1];
    if (c->m_dec_size[src_char] > 0)
    {
      uint *ret;
      // Unpack info is needed but none available.
      if (reader == nullptr)
      {
        return UNPACK_INFO_MISSING;
      }

      if ((ret= reader->read(c->m_dec_size[src_char])) == nullptr)
      {
        return UNPACK_FAILURE;
      }

      if (*ret < c->level)
      {
        dst_char= c->m_dec_idx[*ret][src_char];
      }
      else
      {
        dst_char= c->m_dec_idx_ext[src_char][*ret - c->level];
      }
    }
    else
    {
      dst_char= c->m_dec_idx[0][src_char];
    }

    len= c->m_cs->cset->wc_mb(c->m_cs, dst_char, dst, dst + *dst_len);
    assert(*dst_len >= (size_t)len);
    *dst_len-= len;
    dst+= len;
  }

  return UNPACK_SUCCESS;
}

uint se_read_unpack_utf8mb4(SeBitReader *reader,
                            const SeCollationCodec *codec,
                            const uchar *src,
                            size_t src_len,
                            uchar *dst,
                            size_t *dst_len)
{
  const auto c= static_cast<const Se_collation_codec_utf8*>(codec);
  int len= 0;
  assert(src_len % 2 == 0);
  for (uint i= 0; i < src_len; i+=2)
  {
    my_wc_t src_char, dst_char;
    uint *ret;
    src_char = src[i] << 8 | src[i+1];
    if (src_char == 0xFFFD)
    {
      if ((ret= reader->read(SE_UTF8MB4_LENGTH)) == nullptr)
      {
        return UNPACK_FAILURE;
      }
      dst_char= *ret;
    }
    else
    {
      if (c->m_dec_size[src_char] > 0)
      {
        uint *ret;
        // Unpack info is needed but none available.
        if (reader == nullptr)
        {
          return UNPACK_INFO_MISSING;
        }

        if ((ret= reader->read(c->m_dec_size[src_char])) == nullptr)
        {
          return UNPACK_FAILURE;
        }
        if (*ret < c->level)
        {
          dst_char= c->m_dec_idx[*ret][src_char];
        }
        else
        {
          dst_char= c->m_dec_idx_ext[src_char][*ret - c->level];
        }
      }
      else
      {
        dst_char= c->m_dec_idx[0][src_char];
      }
    }

    len= c->m_cs->cset->wc_mb(c->m_cs, dst_char, dst, dst + *dst_len);
    assert(*dst_len >= (size_t)len);
    *dst_len-= len;
    dst+= len;
  }

  return UNPACK_SUCCESS;
}


uint se_read_unpack_gbk(SeBitReader *reader,
                        const SeCollationCodec *codec,
                        const uchar *src,
                        size_t src_len,
                        uchar *dst,
                        size_t *dst_len)
{
  const auto c= static_cast<const Se_collation_codec_gbk*>(codec);
  int len= 0;
  assert(src_len % 2 == 0);
  for (uint i= 0; i < src_len; i+=2)
  {
    my_wc_t src_char, dst_char, gbk_high;
    // prefix for gbk 1 byte character
    if (src[i] == 0x20)
    {
      src_char= src[i+1];
    }
    else
    {
      src_char= src[i] << 8 | src[i+1];
    }
    if (c->m_dec_size[src_char] > 0)
    {
      uint *ret;
      // Unpack info is needed but none available.
      if (reader == nullptr)
      {
        return UNPACK_INFO_MISSING;
      }

      if ((ret= reader->read(c->m_dec_size[src_char])) == nullptr)
      {
        return UNPACK_FAILURE;
      }

      if (*ret < c->level)
      {
        dst_char= c->m_dec_idx[*ret][src_char];
      }
      else
      {
        dst_char= c->m_dec_idx_ext[src_char][*ret - c->level];
      }
    }
    else
    {
      dst_char= c->m_dec_idx[0][src_char];
    }

    gbk_high= dst_char >> 8;
    len= c->m_cs->cset->mbcharlen(c->m_cs, gbk_high);
    assert(len > 0 && len <= 2);
    if (len == 1)
    {
      *dst= dst_char;
    }
    else
    {
      *dst= dst_char >> 8;
      *(dst+1)= dst_char & 0xFF;
    }

    dst += len;
    if (dst_len)
    {
      *dst_len-= len;
    }
  }

  return UNPACK_SUCCESS;
}

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
                                SeStringReader *unp_reader)
{
  const uchar *ptr;
  size_t len= 0;
  bool finished= false;
  uchar *d0= dst;
  Field_varstring* field_var= static_cast<Field_varstring*>(field);
  uchar *dst_end= dst + field_var->pack_length();
  size_t dst_len = field_var->pack_length() - field_var->get_length_bytes();
  dst += field_var->get_length_bytes();
  SeBitReader bit_reader(unp_reader);

  uint space_padding_bytes= 0;
  uint extra_spaces;
  if (!unp_reader)
  {
    return UNPACK_INFO_MISSING;
  }

  if ((fpi->m_unpack_info_uses_two_bytes?
       unp_reader->read_uint16(&extra_spaces):
       unp_reader->read_uint8(&extra_spaces)))
  {
    return UNPACK_FAILURE;
  }

  if (extra_spaces <= 8)
  {
    space_padding_bytes= -(static_cast<int>(extra_spaces) - 8);
    extra_spaces= 0;
  }
  else
    extra_spaces -= 8;

  space_padding_bytes *= fpi->space_xfrm_len;

  /* Decode the length-emitted encoding here */
  while ((ptr= (const uchar*)reader->read(fpi->m_segment_size)))
  {
    char last_byte= ptr[fpi->m_segment_size - 1];  // number of padding bytes
    size_t used_bytes;
    if (last_byte == VARCHAR_CMP_EQUAL_TO_SPACES)
    {
      // this is the last one
      if (space_padding_bytes > (fpi->m_segment_size-1))
        return UNPACK_FAILURE;  // Cannot happen, corrupted data
      used_bytes= (fpi->m_segment_size-1) - space_padding_bytes;
      finished= true;
    }
    else
    {
      if (last_byte != VARCHAR_CMP_LESS_THAN_SPACES &&
          last_byte != VARCHAR_CMP_GREATER_THAN_SPACES)
      {
        return UNPACK_FAILURE;
      }
      used_bytes= fpi->m_segment_size-1;
    }

    if (dst + used_bytes > dst_end)
    {
      // The value on disk is longer than the field definition allows?
      return UNPACK_FAILURE;
    }

    uint ret;
    auto cset= fpi->m_charset_codec->m_cs;
    if (cset == &my_charset_gbk_chinese_ci)
    {
      if ((ret= se_read_unpack_gbk(&bit_reader,
                                   fpi->m_charset_codec, ptr, used_bytes,
                                   dst, &dst_len)) != UNPACK_SUCCESS)
      {
        return ret;
      }
    }
    else if (cset == &my_charset_utf8mb3_general_ci)
    {
      if ((ret= se_read_unpack_utf8(&bit_reader,
                                   fpi->m_charset_codec, ptr, used_bytes,
                                   dst, &dst_len)) != UNPACK_SUCCESS)
      {
        return ret;
      }
    }
    else if (cset == &my_charset_utf8mb4_general_ci)
    {
      if ((ret= se_read_unpack_utf8mb4(&bit_reader,
                                   fpi->m_charset_codec, ptr, used_bytes,
                                   dst, &dst_len)) != UNPACK_SUCCESS)
      {
        return ret;
      }
    }

    dst = dst_end - dst_len;
    len = dst_end - d0 - dst_len - field_var->get_length_bytes();

    if (finished)
    {
      if (extra_spaces)
      {
        if (dst + extra_spaces > dst_end)
          return UNPACK_FAILURE;
        // pad_char has a 1-byte form in all charsets that
        // are handled by se_init_collation_mapping.
        memset(dst, field_var->charset()->pad_char, extra_spaces);
        len += extra_spaces;
      }
      break;
    }
  }

  if (!finished)
    return UNPACK_FAILURE;

  /* Save the length */
  if (field_var->get_length_bytes() == 1)
  {
    d0[0]= len;
  }
  else
  {
    assert(field_var->get_length_bytes() == 2);
    int2store(d0, len);
  }
  return UNPACK_SUCCESS;
}

int se_unpack_char(SeFieldPacking *fpi,
                   Field *field,
                   uchar *dst,
                   SeStringReader *reader,
                   SeStringReader *unp_reader)
{
  const uchar *ptr;
  int ret= UNPACK_SUCCESS;
  uint len = fpi->m_max_image_len;
  uchar *dst_end= dst + field->pack_length();
  size_t dst_len= field->pack_length();
  SeBitReader bit_reader(unp_reader);
  auto cset= fpi->m_charset_codec->m_cs;

  if (!(ptr= (const uchar*)reader->read(len)))
  {
    return UNPACK_FAILURE;
  }

  if (cset == &my_charset_utf8mb3_general_ci)
  {
    ret = se_read_unpack_utf8(unp_reader ? &bit_reader : nullptr,
                             fpi->m_charset_codec, ptr, len, dst, &dst_len);
  }
  else if (cset == &my_charset_gbk_chinese_ci)
  {
    ret = se_read_unpack_gbk(unp_reader ? &bit_reader : nullptr,
                             fpi->m_charset_codec, ptr, len, dst, &dst_len);
  }
  else if (cset == &my_charset_utf8mb4_general_ci)
  {
    ret = se_read_unpack_utf8mb4(unp_reader ? &bit_reader : nullptr,
                             fpi->m_charset_codec, ptr, len, dst, &dst_len);
  }

  if (ret == UNPACK_SUCCESS)
  {
    cset->cset->fill(cset, reinterpret_cast<char *>(dst_end - dst_len),
                     dst_len, cset->pad_char);
  }
  return ret;
}

} //namespace smartengine
