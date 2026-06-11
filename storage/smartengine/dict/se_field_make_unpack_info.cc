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
#include "se_field_make_unpack_info.h"
#include "sql/field.h"
#include "se_field_pack.h"
#include "se_charset_info.h"

namespace smartengine {

/*
  Function of type se_make_unpack_info_t
*/

void se_make_unpack_unknown(const SeCollationCodec *codec,
                            Field *const field,
                            SePackFieldContext *const pack_ctx)
{
  pack_ctx->writer->write(field->field_ptr(), field->pack_length());
}

/*
  This point of this function is only to indicate that unpack_info is
  available.

  The actual unpack_info data is produced by the function that packs the key,
  that is, se_pack_with_varchar_space_pad.
*/

void se_dummy_make_unpack_info(const SeCollationCodec *codec,
                               Field *field,
                               SePackFieldContext *pack_ctx)
{}

void se_make_unpack_unknown_varchar(const SeCollationCodec *const codec,
                                    Field *const field,
                                    SePackFieldContext *const pack_ctx)
{
  const auto f = static_cast<const Field_varstring *>(field);
  uint len = f->get_length_bytes() == 1 ? (uint)*f->field_ptr() : uint2korr(f->field_ptr());
  len += f->get_length_bytes();
  pack_ctx->writer->write(field->field_ptr(), len);
}

/*
  Write unpack_data for a "simple" collation
*/
void se_write_unpack_simple(SeBitWriter *const writer,
                            const SeCollationCodec *const codec,
                            const uchar *const src,
                            const size_t src_len)
{
  const auto c= static_cast<const Se_collation_codec_simple*>(codec);
  for (uint i = 0; i < src_len; i++) {
    writer->write(c->m_enc_size[src[i]], c->m_enc_idx[src[i]]);
  }
}

/*
  Function of type se_make_unpack_info_t

  @detail
    Make unpack_data for VARCHAR(n) in a "simple" charset.
*/

void se_make_unpack_simple_varchar(const SeCollationCodec *const codec,
                                   Field * field,
                                   SePackFieldContext *const pack_ctx)
{
  auto f = static_cast<Field_varstring *>(field);
  uchar *const src = f->field_ptr() + f->get_length_bytes();
  const size_t src_len =
      f->get_length_bytes() == 1 ? (uint)*f->field_ptr() : uint2korr(f->field_ptr());
  SeBitWriter bit_writer(pack_ctx->writer);
  // The std::min compares characters with bytes, but for simple collations,
  // mbmaxlen = 1.
  se_write_unpack_simple(&bit_writer, codec, src,
                          std::min((size_t)f->char_length(), src_len));
}

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
                           SePackFieldContext *const pack_ctx)
{
  const uchar *const src = field->field_ptr();
  SeBitWriter bit_writer(pack_ctx->writer);
  se_write_unpack_simple(&bit_writer, codec, src, field->pack_length());
}

/*
  Write unpack_data for a utf8 collation
*/
void se_write_unpack_utf8(SeBitWriter *writer,
                          const SeCollationCodec *codec,
                          const uchar *src, size_t src_len,
                          bool is_varchar)
{
  const auto c= static_cast<const Se_collation_codec_utf8*>(codec);
  my_wc_t src_char;
  uint i;
  uint max_bytes= 0; //for char
  for (i= 0; i < src_len && max_bytes < src_len;)
  {
    int res= c->m_cs->cset->mb_wc(c->m_cs, &src_char, src + i, src + src_len);
    assert(res > 0 && res <= 3);
    writer->write(c->m_enc_size[src_char], c->m_enc_idx[src_char]);

    i+= res;
    if (!is_varchar)
    {
      max_bytes+= 3; //max bytes for every utf8 character.
    }
  }
  assert(i <= src_len);
}

/*
  Write unpack_data for a utf8mb4 collation
*/
void se_write_unpack_utf8mb4(SeBitWriter *writer,
                             const SeCollationCodec *codec,
                             const uchar *src,
                             size_t src_len,
                             bool is_varchar)
{
  auto *c= static_cast<const Se_collation_codec_utf8mb4 *>(codec);
  uint i;
  uint max_bytes= 0; //for char
  for (i= 0; i < src_len && max_bytes < src_len;)
  {
    my_wc_t wc;
    int res;
    uchar weight_buf[2];

    res= c->m_cs->cset->mb_wc(c->m_cs, &wc, src + i, src + src_len);
    assert(res > 0 && res <= 4);

    c->m_cs->coll->strnxfrm(c->m_cs, weight_buf, 2, 1, src + i, src_len, MY_STRXFRM_NOPAD_WITH_SPACE);
    /* for utf8mb4 characters, write src into unpack_info, others use codec->m_enc_idx[wc] */
    if (weight_buf[0] == 0xFF && weight_buf[1] == 0xFD)
    {
      writer->write(SE_UTF8MB4_LENGTH, wc);
    }
    else
    {
      writer->write(c->m_enc_size[wc], c->m_enc_idx[wc]);
    }

    i+= res;
    if (!is_varchar)
    {
      max_bytes+= 4; //max bytes for every utf8 character.
    }
  }

  assert(i <= src_len);
}

/*
  Write unpack_data for a gbk collation
*/
void se_write_unpack_gbk(SeBitWriter *writer,
                         const SeCollationCodec *codec,
                         const uchar *src,
                         size_t src_len,
                         bool is_varchar)
{
  const auto c= static_cast<const Se_collation_codec_gbk*>(codec);
  uint i;
  uint max_bytes = 0;
  for (i= 0; i < src_len && max_bytes < src_len;)
  {
    my_wc_t wc;
    int res;

    res= c->m_cs->cset->mbcharlen(c->m_cs, (uint)(src[i]));
    if (res == 1)
    {
      wc= src[i];
    }
    else
    {
      wc= src[i] << 8;
      wc= wc | src[i+1];
    }

    writer->write(c->m_enc_size[wc], c->m_enc_idx[wc]);

    i+= res;
    if (!is_varchar)
    {
      max_bytes+= 2; //max bytes for every utf8 character.
    }
  }
  assert(i <= src_len);
}

/*
  Function of type se_make_unpack_info_t

  @detail
    Make unpack_data for VARCHAR(n) in a specific charset.
*/

void se_make_unpack_varchar(const SeCollationCodec* codec,
                            Field *field,
                            SePackFieldContext *pack_ctx)
{
  auto f= static_cast<Field_varstring *>(field);
  uchar *src= f->field_ptr() + f->get_length_bytes();
  size_t src_len= f->get_length_bytes() == 1 ? (uint) *f->field_ptr() : uint2korr(f->field_ptr());

  SeBitWriter bit_writer(pack_ctx->writer);

  //if column-prefix is part of key, we don't support index-only-scan
  //so, src_len is ok for write unpack_info.
  if (codec->m_cs == &my_charset_utf8mb3_general_ci)
  {
    se_write_unpack_utf8(&bit_writer, codec, src, src_len, true);
  }
  else if (codec->m_cs == &my_charset_gbk_chinese_ci)
  {
    se_write_unpack_gbk(&bit_writer, codec, src, src_len, true);
  }
  else if (codec->m_cs == &my_charset_utf8mb4_general_ci)
  {
    se_write_unpack_utf8mb4(&bit_writer, codec, src, src_len, true);
  }
}

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
                         SePackFieldContext *pack_ctx)
{
  uchar *src= field->field_ptr();
  SeBitWriter bit_writer(pack_ctx->writer);
  if (codec->m_cs == &my_charset_utf8mb3_general_ci)
  {
    se_write_unpack_utf8(&bit_writer, codec, src, field->pack_length(), false);
  }
  else if (codec->m_cs == &my_charset_gbk_chinese_ci)
  {
    se_write_unpack_gbk(&bit_writer, codec, src, field->pack_length(), false);
  }
  else if (codec->m_cs == &my_charset_utf8mb4_general_ci)
  {
    se_write_unpack_utf8mb4(&bit_writer, codec, src, field->pack_length(), false);
  }
}

} //namespace smartengine
