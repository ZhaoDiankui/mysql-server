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
#include "se_field_pack_func.h"
#include "se_field_pack.h"
#include "myisampack.h"
#include "sql_class.h"
#include "field.h"

namespace smartengine
{

/*
 * From Field_blob::make_sort_key in MySQL 5.7
 */
void se_pack_blob(SeFieldPacking *const fpi,
                  Field *const field,
                  uchar *const buf,
                  uchar **dst,
                  SePackFieldContext *const pack_ctx)
{
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_TINY_BLOB ||
         field->real_type() == MYSQL_TYPE_MEDIUM_BLOB ||
         field->real_type() == MYSQL_TYPE_LONG_BLOB ||
         field->real_type() == MYSQL_TYPE_BLOB ||
         field->real_type() == MYSQL_TYPE_JSON);

  int64_t length = fpi->m_max_image_len;
  Field_blob* field_blob = static_cast<Field_blob *>(field);
  const CHARSET_INFO *field_charset = field_blob->charset();

  uchar *blob = nullptr;
  int64_t blob_length = field_blob->get_length();

  if (!blob_length && field_charset->pad_char == 0) {
    memset(*dst, 0, length);
  } else {
    if (field_charset == &my_charset_bin) {
      /*
        Store length of blob last in blob to shorter blobs before longer blobs
      */
      length -= field_blob->pack_length_no_ptr();
      uchar *pos = *dst + length;
      int64_t key_length = blob_length < length ? blob_length : length;

      switch (field_blob->pack_length_no_ptr()) {
      case 1:
        *pos = (char)key_length;
        break;
      case 2:
        mi_int2store(pos, key_length);
        break;
      case 3:
        mi_int3store(pos, key_length);
        break;
      case 4:
        mi_int4store(pos, key_length);
        break;
      }
    }

    // Copy a ptr
    memcpy(&blob, field->field_ptr() + field_blob->pack_length_no_ptr(), sizeof(char *));

    //weights for utf8mb4_900 and gbk are not same length for diffent characters
    //for utf8mb4_0900_ai_ci prefix text index, see also se_pack_with_varchar_encoding
    if (fpi->m_prefix_index_flag &&
        (field_charset == &my_charset_utf8mb4_0900_ai_ci ||
         field_charset == &my_charset_gbk_chinese_ci ||
         field_charset == &my_charset_gbk_bin)) {
      assert(fpi->m_weights > 0);
      int input_length = std::min<size_t>(
          blob_length,
          field_charset->cset->charpos(
              field_charset, pointer_cast<const char *>(blob),
              pointer_cast<const char *>(blob) + blob_length, fpi->m_weights));

      blob_length = blob_length <= input_length ? blob_length : input_length;
    }

    if (blob == nullptr) {
      //in case, for sql_mode is not in the strict mode, if doing nullable to not null
      //ddl-change, field null value will change to empty string, but blob address is nullptr.
      //so, we use dummy address for padding
      blob = pointer_cast<uchar*>(&blob);
      blob_length = 0;
    }

    blob_length =
        field_charset->coll->strnxfrm(field_charset, *dst, length, length, blob,
                                      blob_length, MY_STRXFRM_PAD_TO_MAXLEN);
    assert(blob_length == length);
  }

  *dst += fpi->m_max_image_len;
}

#if !defined(DBL_EXP_DIG)
#define DBL_EXP_DIG (sizeof(double) * 8 - DBL_MANT_DIG)
#endif

void change_double_for_sort(double nr, uchar *to)
{
  uchar *tmp = to;
  if (nr == 0.0) { /* Change to zero string */
    tmp[0] = (uchar)128;
    memset(tmp + 1, 0, sizeof(nr) - 1);
  } else {
#ifdef WORDS_BIGENDIAN
    memcpy(tmp, &nr, sizeof(nr));
#else
    {
      uchar *ptr = (uchar *)&nr;
#if defined(__FLOAT_WORD_ORDER) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)
      tmp[0] = ptr[3];
      tmp[1] = ptr[2];
      tmp[2] = ptr[1];
      tmp[3] = ptr[0];
      tmp[4] = ptr[7];
      tmp[5] = ptr[6];
      tmp[6] = ptr[5];
      tmp[7] = ptr[4];
#else
      tmp[0] = ptr[7];
      tmp[1] = ptr[6];
      tmp[2] = ptr[5];
      tmp[3] = ptr[4];
      tmp[4] = ptr[3];
      tmp[5] = ptr[2];
      tmp[6] = ptr[1];
      tmp[7] = ptr[0];
#endif
    }
#endif
    if (tmp[0] & 128) /* Negative */
    {                 /* make complement */
      uint i;
      for (i = 0; i < sizeof(nr); i++)
        tmp[i] = tmp[i] ^ (uchar)255;
    } else { /* Set high and move exponent one up */
      ushort exp_part =
          (((ushort)tmp[0] << 8) | (ushort)tmp[1] | (ushort)32768);
      exp_part += (ushort)1 << (16 - 1 - DBL_EXP_DIG);
      tmp[0] = (uchar)(exp_part >> 8);
      tmp[1] = (uchar)exp_part;
    }
  }
}

void se_pack_double(SeFieldPacking *const fpi,
                    Field *const field,
                    uchar *const buf,
                    uchar **dst,
                    SePackFieldContext *const pack_ctx)
{
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_DOUBLE);

  const size_t length = fpi->m_max_image_len;
  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;

  double nr;
#ifdef WORDS_BIGENDIAN
  if (field->table->s->db_low_byte_first) {
    float8get(&nr, ptr);
  } else
#endif
    nr = doubleget(ptr);
    //doubleget(&nr, ptr);

  if (length < 8) {
    uchar buff[8];
    change_double_for_sort(nr, buff);
    memcpy(to, buff, length);
  } else
    change_double_for_sort(nr, to);

  *dst += length;
}

#if !defined(FLT_EXP_DIG)
#define FLT_EXP_DIG (sizeof(float) * 8 - FLT_MANT_DIG)
#endif

void se_pack_float(SeFieldPacking *const fpi,
                   Field *const field,
                   uchar *const buf,
                   uchar **dst,
                   SePackFieldContext *const pack_ctx)
{
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_FLOAT);

  const size_t length = fpi->m_max_image_len;
  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;

  assert(length == sizeof(float));
  float nr;

#ifdef WORDS_BIGENDIAN
  if (field->table->s->db_low_byte_first) {
    float4get(&nr, ptr);
  } else
#endif
    memcpy(&nr, ptr, length < sizeof(float) ? length : sizeof(float));

  uchar *tmp = to;
  if (nr == (float)0.0) { /* Change to zero string */
    tmp[0] = (uchar)128;
    memset(tmp + 1, 0, length < sizeof(nr) - 1 ? length : sizeof(nr) - 1);
  } else {
#ifdef WORDS_BIGENDIAN
    memcpy(tmp, &nr, sizeof(nr));
#else
    tmp[0] = ptr[3];
    tmp[1] = ptr[2];
    tmp[2] = ptr[1];
    tmp[3] = ptr[0];
#endif
    if (tmp[0] & 128) /* Negative */
    {                 /* make complement */
      uint i;
      for (i = 0; i < sizeof(nr); i++)
        tmp[i] = (uchar)(tmp[i] ^ (uchar)255);
    } else {
      ushort exp_part =
          (((ushort)tmp[0] << 8) | (ushort)tmp[1] | (ushort)32768);
      exp_part += (ushort)1 << (16 - 1 - FLT_EXP_DIG);
      tmp[0] = (uchar)(exp_part >> 8);
      tmp[1] = (uchar)exp_part;
    }
  }

  *dst += length;
}

/*
  Function of type se_index_field_pack_t
*/
void se_pack_with_make_sort_key(SeFieldPacking *const fpi,
                                Field *const field,
                                uchar *const buf,
                                uchar **dst,
                                SePackFieldContext *const pack_ctx)
{
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);

  const int max_len = fpi->m_max_image_len;
  field->make_sort_key(*dst, max_len);
  *dst += max_len;
}


/** Function of type se_index_field_pack_t for utf8mb4_0900_aici MYSQL_TYPE_STRING
 *  for that m_max_image_len is max size of weights, it's not suitable for utf8mb4_0900_ai_ci, every character maybe occupy different bytes for weight.
 *  see also Field_string::make_sort_key
 */
void se_pack_with_make_sort_key_utf8mb4_0900(SeFieldPacking *fpi,
                                             Field *field,
                                             uchar *buf,
                                             uchar **dst,
                                             SePackFieldContext *pack_ctx)
{
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);

  const CHARSET_INFO *cs= fpi->m_charset;
  const int max_len= fpi->m_max_image_len;

  assert(fpi->m_weights > 0);
  assert(fpi->m_weights <= field->char_length());

  if (fpi->m_prefix_index_flag == false) {
    field->make_sort_key(*dst, max_len);
  } else {
    uint nweights = fpi->m_weights;

    uint field_length = field->field_length;
    size_t input_length = std::min<size_t>(
        field_length,
        cs->cset->charpos(cs, pointer_cast<const char *>(field->field_ptr()),
                          pointer_cast<const char *>(field->field_ptr()) + field_length,
                          nweights));

    TABLE *table = ((Field_string *)field)->table;
    if (cs->pad_attribute == NO_PAD &&
        !(table->in_use->variables.sql_mode & MODE_PAD_CHAR_TO_FULL_LENGTH)) {
      /*
        Our CHAR default behavior is to strip spaces. For PAD SPACE collations,
        this doesn't matter, for but NO PAD, we need to do it ourselves here.
      */
      input_length =
          cs->cset->lengthsp(cs, (const char *)(field->field_ptr()), input_length);
    }

    int xfrm_len = cs->coll->strnxfrm(cs, *dst, fpi->m_max_strnxfrm_len,
                                      field->char_length(), field->field_ptr(),
                                      input_length, MY_STRXFRM_PAD_TO_MAXLEN);
    assert(xfrm_len <= max_len);
  }

  *dst += max_len;
}

/*
  Function of type se_index_field_pack_t for gbk MYSQL_TYPE_STRING
*/
void se_pack_with_make_sort_key_gbk(SeFieldPacking *fpi,
                                    Field *field,
                                    uchar *buf,
                                    uchar **dst,
                                    SePackFieldContext *pack_ctx)
{
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);

  const CHARSET_INFO *cs= fpi->m_charset;
  const int max_len= fpi->m_max_image_len;
  uchar *src= field->field_ptr();
  uint srclen= field->max_display_length();
  uchar *se= src + srclen;
  uchar *dst_buf= *dst;
  uchar *de= *dst + max_len;
  size_t xfrm_len_total= 0;
  uint nweights = field->char_length();

  assert(fpi->m_weights > 0);
  assert(fpi->m_weights <= nweights);

  if (fpi->m_prefix_index_flag == true) {
    nweights= fpi->m_weights;
  }

  for (; dst_buf < de && src < se && nweights; nweights--)
  {
    if (cs->cset->ismbchar(cs, (const char*)src, (const char*)se))
    {
      cs->coll->strnxfrm(cs, dst_buf, 2, 1, src, 2, MY_STRXFRM_NOPAD_WITH_SPACE);
      src += 2;
      srclen -= 2;
    }
    else
    {
      cs->coll->strnxfrm(cs, dst_buf, 1, 1, src, 1, MY_STRXFRM_NOPAD_WITH_SPACE);
      src += 1;
      srclen -= 1;

      /* add 0x20 for 2bytes comparebytes */
      *(dst_buf + 1)= *dst_buf;
      *dst_buf= 0x20;
    }

    dst_buf += 2;
    xfrm_len_total += 2;
  }

  int tmp __attribute__((unused))=
    cs->coll->strnxfrm(cs,
                       dst_buf, max_len-xfrm_len_total, nweights,
                       src, srclen,
                       MY_STRXFRM_PAD_TO_MAXLEN);

  assert(((int)xfrm_len_total + tmp) == max_len);

  *dst += max_len;
}

void se_pack_with_varchar_encoding(SeFieldPacking *const fpi,
                                   Field *const field,
                                   uchar *buf,
                                   uchar **dst,
                                   SePackFieldContext *const pack_ctx)
{
  /*
    Use a flag byte every Nth byte. Set it to (255 - #pad) where #pad is 0
    when the var length field filled all N-1 previous bytes and #pad is
    otherwise the number of padding bytes used.

    If N=8 and the field is:
    * 3 bytes (1, 2, 3) this is encoded as: 1, 2, 3, 0, 0, 0, 0, 251
    * 4 bytes (1, 2, 3, 0) this is encoded as: 1, 2, 3, 0, 0, 0, 0, 252
    And the 4 byte string compares as greater than the 3 byte string
  */
  const CHARSET_INFO *const charset = field->charset();
  Field_varstring *const field_var = (Field_varstring *)field;


  //actual length of data column
  size_t value_length = (field_var->get_length_bytes() == 1) ?
                        (uint)*field->field_ptr() : uint2korr(field->field_ptr());

  // if prefix-column index is less than value length, use prefix-column-length;
  // otherwise use value length.
  if (fpi->m_prefix_index_flag == true && charset == &my_charset_utf8mb4_0900_ai_ci) {
    assert(fpi->m_weights > 0);
    int length_bytes = field_var->get_length_bytes();
    size_t input_length = std::min<size_t>(
        value_length,
        charset->cset->charpos(
            charset, pointer_cast<const char *>(field->field_ptr() + length_bytes),
            pointer_cast<const char *>(field->field_ptr() + length_bytes) +
                value_length,
            fpi->m_weights));

    value_length = value_length <= input_length ? value_length : input_length;
  }

  size_t xfrm_len = charset->coll->strnxfrm(
      charset, buf, fpi->m_max_strnxfrm_len, field_var->char_length(),
      field_var->field_ptr() + field_var->get_length_bytes(), value_length,
      MY_STRXFRM_NOPAD_WITH_SPACE);

  /* Got a mem-comparable image in 'buf'. Now, produce varlength encoding */

  size_t encoded_size = 0;
  uchar *ptr = *dst;
  while (1) {
    const size_t copy_len = std::min((size_t)SE_ESCAPE_LENGTH - 1, xfrm_len);
    const size_t padding_bytes = SE_ESCAPE_LENGTH - 1 - copy_len;
    memcpy(ptr, buf, copy_len);
    ptr += copy_len;
    buf += copy_len;
    // pad with zeros if necessary;
    for (size_t idx = 0; idx < padding_bytes; idx++)
      *(ptr++) = 0;
    *(ptr++) = 255 - padding_bytes;

    xfrm_len -= copy_len;
    encoded_size += SE_ESCAPE_LENGTH;
    if (padding_bytes != 0)
      break;
  }
  *dst += encoded_size;
}

/*
  Compare the string in [buf..buf_end) with a string that is an infinite
  sequence of strings in space_xfrm
*/

int se_compare_string_with_spaces(const uchar *buf,
                                  const uchar *const buf_end,
                                  const std::vector<uchar> *const space_xfrm)
{
  int cmp = 0;
  while (buf < buf_end) {
    size_t bytes = std::min((size_t)(buf_end - buf), space_xfrm->size());
    if ((cmp = memcmp(buf, space_xfrm->data(), bytes)) != 0)
      break;
    buf += bytes;
  }
  return cmp;
}

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
                                    SePackFieldContext *pack_ctx)
{
  SeStringWriter *unpack_info= pack_ctx->writer;
  const CHARSET_INFO *charset= field->charset();
  auto field_var= static_cast<Field_varstring *>(field);

  size_t value_length= (field_var->get_length_bytes() == 1) ?
                       (uint) *field->field_ptr() : uint2korr(field->field_ptr());

  size_t trimmed_len=
    charset->cset->lengthsp(charset,
                            (const char*)field_var->field_ptr() +
                                         field_var->get_length_bytes(),
                            value_length);

  uchar *buf_end;
  if (charset == &my_charset_gbk_chinese_ci || charset == &my_charset_gbk_bin)
  {
    uchar *src= field_var->field_ptr() + field_var->get_length_bytes();
    uchar *se= src + trimmed_len;
    uchar *dst_buf= buf;
    assert(fpi->m_weights > 0);

    uint nweights = trimmed_len;
    if (fpi->m_prefix_index_flag == true) {
      nweights= fpi->m_weights <= trimmed_len ? fpi->m_weights : trimmed_len;
    }

    for (; src < se && nweights; nweights--)
    {
      if (charset->cset->ismbchar(charset, (const char*)src, (const char*)se))
      {
        charset->coll->strnxfrm(charset, dst_buf, 2, 1, src, 2, MY_STRXFRM_NOPAD_WITH_SPACE);
        src += 2;
      }
      else
      {
        charset->coll->strnxfrm(charset, dst_buf, 1, 1, src, 1, MY_STRXFRM_NOPAD_WITH_SPACE);
        src += 1;

        /* add 0x20 for 2bytes comparebytes */
        *(dst_buf + 1)= *dst_buf;
        *dst_buf= 0x20;
      }

      dst_buf += 2;
    }

    buf_end= dst_buf;
  }
  else
  {
   /* for utf8 and utf8mb4 */
    size_t xfrm_len;
    xfrm_len= charset->coll->strnxfrm(charset,
                                      buf, fpi->m_max_strnxfrm_len,
                                      field_var->char_length(),
                                      field_var->field_ptr() + field_var->get_length_bytes(),
                                      trimmed_len,
                                      MY_STRXFRM_NOPAD_WITH_SPACE);

    /* Got a mem-comparable image in 'buf'. Now, produce varlength encoding */
    buf_end= buf + xfrm_len;
  }

  size_t encoded_size= 0;
  uchar *ptr= *dst;
  size_t padding_bytes;
  while (true)
  {
    size_t copy_len= std::min<size_t>(fpi->m_segment_size-1, buf_end - buf);
    padding_bytes= fpi->m_segment_size - 1 - copy_len;

    memcpy(ptr, buf, copy_len);
    ptr += copy_len;
    buf += copy_len;

    if (padding_bytes)
    {
      memcpy(ptr, fpi->space_xfrm->data(), padding_bytes);
      ptr+= padding_bytes;
      *ptr= VARCHAR_CMP_EQUAL_TO_SPACES;  // last segment
    }
    else
    {
      // Compare the string suffix with a hypothetical infinite string of
      // spaces. It could be that the first difference is beyond the end of
      // current chunk.
      int cmp= se_compare_string_with_spaces(buf, buf_end, fpi->space_xfrm);

      if (cmp < 0)
        *ptr= VARCHAR_CMP_LESS_THAN_SPACES;
      else if (cmp > 0)
        *ptr= VARCHAR_CMP_GREATER_THAN_SPACES;
      else
      {
        // It turns out all the rest are spaces.
        *ptr= VARCHAR_CMP_EQUAL_TO_SPACES;
      }
    }
    encoded_size += fpi->m_segment_size;

    if (*(ptr++) == VARCHAR_CMP_EQUAL_TO_SPACES)
      break;
  }

  // m_unpack_info_stores_value means unpack_info stores the whole original
  // value. There is no need to store the number of trimmed/padded endspaces
  // in that case.
  if (unpack_info && !fpi->m_unpack_info_stores_value)
  {
    // (value_length - trimmed_len) is the number of trimmed space *characters*
    // then, padding_bytes is the number of *bytes* added as padding
    // then, we add 8, because we don't store negative values.
    assert(padding_bytes % fpi->space_xfrm_len == 0);
    assert((value_length - trimmed_len)% fpi->space_mb_len == 0);
    size_t removed_chars= SE_TRIMMED_CHARS_OFFSET +
                          (value_length - trimmed_len) / fpi->space_mb_len -
                          padding_bytes/fpi->space_xfrm_len;

    if (fpi->m_unpack_info_uses_two_bytes)
    {
      unpack_info->write_uint16(removed_chars);
    }
    else
    {
      assert(removed_chars < 0x100);
      unpack_info->write_uint8(removed_chars);
    }
  }

  *dst += encoded_size;
}

} //namespace smartengine
