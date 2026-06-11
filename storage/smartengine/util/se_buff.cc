/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Portions Copyright (c) 2016-Present, Facebook, Inc.
   Portions Copyright (c) 2012,2013 Monty Program Ab

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

#ifndef SMARTENGINE_UTIL_BUFF_H_
#define SMARTENGINE_UTIL_BUFF_H_

#include "se_buff.h"
#include "sql/handler.h"

namespace smartengine
{

std::string format_string(const char *const format, ...)
{
  std::string res;
  va_list args;
  va_list args_copy;
  char static_buff[256];

  assert(format != nullptr);

  va_start(args, format);
  va_copy(args_copy, args);

  // Calculate how much space we will need
  int len = vsnprintf(nullptr, 0, format, args);
  va_end(args);

  if (len < 0) {
    res = std::string("<format error>");
  } else if (len == 0) {
    // Shortcut for an empty string
    res = std::string("");
  } else {
    // For short enough output use a static buffer
    char *buff = static_buff;
    std::unique_ptr<char[]> dynamic_buff = nullptr;

    len++; // Add one for null terminator

    // for longer output use an allocated buffer
    if (static_cast<uint>(len) > sizeof(static_buff)) {
      dynamic_buff.reset(new char[len]);
      buff = dynamic_buff.get();
    }

    // Now re-do the vsnprintf with the buffer which is now large enough
    (void)vsnprintf(buff, len, format, args_copy);

    // Convert to a std::string.  Note we could have created a std::string
    // large enough and then converted the buffer to a 'char*' and created
    // the output in place.  This would probably work but feels like a hack.
    // Since this isn't code that needs to be super-performant we are going
    // with this 'safer' method.
    res = std::string(buff);
  }

  va_end(args_copy);

  return res;
}

/*
 * Serializes an xid to a string so that it can
 * be used as a se transaction name
 */
std::string se_xid_to_string(const XID &src) {
  assert(src.get_gtrid_length() >= 0 && src.get_gtrid_length() <= MAXGTRIDSIZE);
  assert(src.get_bqual_length() >= 0 && src.get_bqual_length() <= MAXBQUALSIZE);

  std::string buf;
  buf.reserve(SE_XIDHDR_LEN + src.get_gtrid_length() + src.get_bqual_length());

  /*
   * expand formatID to fill 8 bytes if it doesn't already
   * then reinterpret bit pattern as unsigned and store in network order
   */
  uchar fidbuf[SE_FORMATID_SZ];
  int64 signed_fid8 = src.get_format_id();
  const uint64 raw_fid8 = *reinterpret_cast<uint64 *>(&signed_fid8);
  se_netbuf_store_uint64(fidbuf, raw_fid8);
  buf.append(reinterpret_cast<const char *>(fidbuf), SE_FORMATID_SZ);

  buf.push_back(src.get_gtrid_length());
  buf.push_back(src.get_bqual_length());
  buf.append(src.get_data(), (src.get_gtrid_length()) + (src.get_bqual_length()));
  return buf;
}

/**
  Rebuilds an XID from a serialized version stored in a string.
*/
void se_xid_from_string(const std::string &src, XID *const dst) {
  assert(dst != nullptr);
  uint offset = 0;
  uint64 raw_fid8 =
      se_netbuf_to_uint64(reinterpret_cast<const uchar *>(src.data()));
  const int64 signed_fid8 = *reinterpret_cast<int64 *>(&raw_fid8);
  dst->set_format_id(signed_fid8);
  offset += SE_FORMATID_SZ;
  dst->set_gtrid_length(src.at(offset));
  offset += SE_GTRID_SZ;
  dst->set_bqual_length(src.at(offset));
  offset += SE_BQUAL_SZ;

  assert(dst->get_gtrid_length() >= 0 && dst->get_gtrid_length() <= MAXGTRIDSIZE);
  assert(dst->get_bqual_length() >= 0 && dst->get_bqual_length() <= MAXBQUALSIZE);

  const std::string &tmp_data = src.substr(
      SE_XIDHDR_LEN, (dst->get_gtrid_length()) + (dst->get_bqual_length()));
  dst->set_data(tmp_data.data(), tmp_data.length());
}

} // namespace smartengine

#endif // end of SMARTENGINE_UTIL_BUFF_H_
