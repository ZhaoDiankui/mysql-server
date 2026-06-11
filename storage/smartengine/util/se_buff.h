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
#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include "sql/xa.h"
#include "se_utils.h"

namespace smartengine
{

std::string format_string(const char *const format, ...);

/*
  Defines the field sizes for serializing XID object to a string representation.
  string byte format: [field_size: field_value, ...]
  [
    8: XID.formatID,
    1: XID.gtrid_length,
    1: XID.bqual_length,
    XID.gtrid_length + XID.bqual_length: XID.data
  ]
*/
#define SE_FORMATID_SZ 8
#define SE_GTRID_SZ 1
#define SE_BQUAL_SZ 1
#define SE_XIDHDR_LEN (SE_FORMATID_SZ + SE_GTRID_SZ + SE_BQUAL_SZ)

/*
 * Serializes an xid to a string so that it can
 * be used as a se transaction name
 */
std::string se_xid_to_string(const XID &src);


/**
  Rebuilds an XID from a serialized version stored in a string.
*/
void se_xid_from_string(const std::string &src, XID *const dst);

/*
  Basic composition functions for a network buffer presented as a MySQL String
  ("netstr") which stores data in Network Byte Order (Big Endian).
*/

inline void se_netstr_append_uint64(my_core::String *const out_netstr,
                                     const uint64 &val) {
  assert(out_netstr != nullptr);

  // Convert from host machine byte order (usually Little Endian) to network
  // byte order (Big Endian).
  uint64 net_val = htobe64(val);
  out_netstr->append(reinterpret_cast<char *>(&net_val), sizeof(net_val));
}

inline void se_netstr_append_uint32(my_core::String *const out_netstr,
                                     const uint32 &val) {
  assert(out_netstr != nullptr);

  // Convert from host machine byte order (usually Little Endian) to network
  // byte order (Big Endian).
  uint32 net_val = htobe32(val);
  out_netstr->append(reinterpret_cast<char *>(&net_val), sizeof(net_val));
}

inline void se_netstr_append_uint16(my_core::String *const out_netstr,
                                     const uint16 &val) {
  assert(out_netstr != nullptr);

  // Convert from host machine byte order (usually Little Endian) to network
  // byte order (Big Endian).
  uint16 net_val = htobe16(val);
  out_netstr->append(reinterpret_cast<char *>(&net_val), sizeof(net_val));
}

/*
  Basic network buffer ("netbuf") write helper functions.
*/

inline void se_netbuf_store_uint64(uchar *const dst_netbuf, const uint64 &n) {
  assert(dst_netbuf != nullptr);

  // Convert from host byte order (usually Little Endian) to network byte order
  // (Big Endian).
  uint64 net_val = htobe64(n);
  memcpy(dst_netbuf, &net_val, sizeof(net_val));
}

inline void se_netbuf_store_uint32(uchar *const dst_netbuf, const uint32 &n) {
  assert(dst_netbuf != nullptr);

  // Convert from host byte order (usually Little Endian) to network byte order
  // (Big Endian).
  uint32 net_val = htobe32(n);
  memcpy(dst_netbuf, &net_val, sizeof(net_val));
}

inline void se_netbuf_store_uint16(uchar *const dst_netbuf, const uint16 &n) {
  assert(dst_netbuf != nullptr);

  // Convert from host byte order (usually Little Endian) to network byte order
  // (Big Endian).
  uint16 net_val = htobe16(n);
  memcpy(dst_netbuf, &net_val, sizeof(net_val));
}

inline void se_netbuf_store_byte(uchar *const dst_netbuf, const uchar &c) {
  assert(dst_netbuf != nullptr);

  *dst_netbuf = c;
}

inline void se_netbuf_store_index(uchar *const dst_netbuf,
                                   const uint32 &number) {
  assert(dst_netbuf != nullptr);

  se_netbuf_store_uint32(dst_netbuf, number);
}

/*
  Basic conversion helper functions from network byte order (Big Endian) to host
  machine byte order (usually Little Endian).
*/

inline uint64 se_netbuf_to_uint64(const uchar *const netbuf) {
  assert(netbuf != nullptr);

  uint64 net_val;
  memcpy(&net_val, netbuf, sizeof(net_val));

  // Convert from network byte order (Big Endian) to host machine byte order
  // (usually Little Endian).
  return be64toh(net_val);
}

inline uint32 se_netbuf_to_uint32(const uchar *const netbuf) {
  assert(netbuf != nullptr);

  uint32 net_val;
  memcpy(&net_val, netbuf, sizeof(net_val));

  // Convert from network byte order (Big Endian) to host machine byte order
  // (usually Little Endian).
  return be32toh(net_val);
}

inline uint16 se_netbuf_to_uint16(const uchar *const netbuf) {
  assert(netbuf != nullptr);

  uint16 net_val;
  memcpy(&net_val, netbuf, sizeof(net_val));

  // Convert from network byte order (Big Endian) to host machine byte order
  // (usually Little Endian).
  return be16toh(net_val);
}

inline uchar se_netbuf_to_byte(const uchar *const netbuf) {
  assert(netbuf != nullptr);

  return (uchar)netbuf[0];
}

/*
  Basic network buffer ("netbuf") read helper functions.
  Network buffer stores data in Network Byte Order (Big Endian).
  NB: The netbuf is passed as an input/output param, hence after reading,
      the netbuf pointer gets advanced to the following byte.
*/

inline uint64 se_netbuf_read_uint64(const uchar **netbuf_ptr) {
  assert(netbuf_ptr != nullptr);

  // Convert from network byte order (Big Endian) to host machine byte order
  // (usually Little Endian).
  const uint64 host_val = se_netbuf_to_uint64(*netbuf_ptr);

  // Advance pointer.
  *netbuf_ptr += sizeof(host_val);

  return host_val;
}

inline uint32 se_netbuf_read_uint32(const uchar **netbuf_ptr) {
  assert(netbuf_ptr != nullptr);

  // Convert from network byte order (Big Endian) to host machine byte order
  // (usually Little Endian).
  const uint32 host_val = se_netbuf_to_uint32(*netbuf_ptr);

  // Advance pointer.
  *netbuf_ptr += sizeof(host_val);

  return host_val;
}

inline uint16 se_netbuf_read_uint16(const uchar **netbuf_ptr) {
  assert(netbuf_ptr != nullptr);

  // Convert from network byte order (Big Endian) to host machine byte order
  // (usually Little Endian).
  const uint16 host_val = se_netbuf_to_uint16(*netbuf_ptr);

  // Advance pointer.
  *netbuf_ptr += sizeof(host_val);

  return host_val;
}

inline void se_netbuf_read_gl_index(const uchar **netbuf_ptr,
                                     GL_INDEX_ID *const gl_index_id) {
  assert(gl_index_id != nullptr);
  assert(netbuf_ptr != nullptr);

  gl_index_id->cf_id = se_netbuf_read_uint32(netbuf_ptr);
  gl_index_id->index_id = se_netbuf_read_uint32(netbuf_ptr);
}

/*
  A simple string reader:
  - it keeps position within the string that we read from
  - it prevents one from reading beyond the end of the string.
*/

class SeStringReader {
  const char *m_ptr;
  uint m_len;

private:
  SeStringReader &operator=(const SeStringReader &) = default;

public:
  SeStringReader(const SeStringReader &) = default;
  /* named constructor */
  static SeStringReader read_or_empty(const smartengine::common::Slice *const slice) {
    if (!slice) {
      return SeStringReader("");
    } else {
      return SeStringReader(slice);
    }
  }

  explicit SeStringReader(const std::string &str) {
    m_len = str.length();
    if (m_len) {
      m_ptr = &str.at(0);
    } else {
      /*
        One can a create a SeStringReader for reading from an empty string
        (although attempts to read anything will fail).
        We must not access str.at(0), since len==0, we can set ptr to any
        value.
      */
      m_ptr = nullptr;
    }
  }

  explicit SeStringReader(const smartengine::common::Slice *const slice) {
    m_ptr = slice->data();
    m_len = slice->size();
  }

  /*
    Read the next @param size bytes. Returns pointer to the bytes read, or
    nullptr if the remaining string doesn't have that many bytes.
  */
  const char *read(const uint &size) {
    const char *res;
    if (m_len < size) {
      res = nullptr;
    } else {
      res = m_ptr;
      m_ptr += size;
      m_len -= size;
    }
    return res;
  }

  bool read_uint8(uint *const res) {
    const uchar *p;
    if (!(p = reinterpret_cast<const uchar *>(read(1))))
      return true; // error
    else {
      *res = *p;
      return false; // Ok
    }
  }

  bool read_uint16(uint *const res) {
    const uchar *p;
    if (!(p = reinterpret_cast<const uchar *>(read(2))))
      return true; // error
    else {
      *res = se_netbuf_to_uint16(p);
      return false; // Ok
    }
  }

  uint remaining_bytes() const { return m_len; }

  /*
    Return pointer to data that will be read by next read() call (if there is
    nothing left to read, returns pointer to beyond the end of previous read()
    call)
  */
  const char *get_current_ptr() const { return m_ptr; }
};

/*
  @brief
  A buffer one can write the data to.

  @detail
  Suggested usage pattern:

    writer->clear();
    writer->write_XXX(...);
    ...
    // Ok, writer->ptr() points to the data written so far,
    // and writer->get_current_pos() is the length of the data

*/

class SeStringWriter {
  std::vector<uchar> m_data;

public:
  SeStringWriter(const SeStringWriter &) = delete;
  SeStringWriter &operator=(const SeStringWriter &) = delete;
  SeStringWriter() = default;

  void clear() { m_data.clear(); }
  void write_uint8(const uint &val) {
    m_data.push_back(static_cast<uchar>(val));
  }

  void write_uint16(const uint &val) {
    const auto size = m_data.size();
    m_data.resize(size + 2);
    se_netbuf_store_uint16(m_data.data() + size, val);
  }

  void write_uint32(const uint &val) {
    const auto size = m_data.size();
    m_data.resize(size + 4);
    se_netbuf_store_uint32(m_data.data() + size, val);
  }

  void write(const uchar *const new_data, const size_t &len) {
    assert(new_data != nullptr);
    m_data.insert(m_data.end(), new_data, new_data + len);
  }

  uchar *ptr() { return m_data.data(); }
  size_t get_current_pos() const { return m_data.size(); }

  void write_uint8_at(const size_t &pos, const uint &new_val) {
    // This function will only overwrite what was written
    assert(pos < get_current_pos());
    m_data.data()[pos] = new_val;
  }

  void write_uint16_at(const size_t &pos, const uint &new_val) {
    // This function will only overwrite what was written
    assert(pos < get_current_pos() && (pos + 1) < get_current_pos());
    se_netbuf_store_uint16(m_data.data() + pos, new_val);
  }
};

/*
   A helper class for writing bits into SeStringWriter.

   The class assumes (but doesn't check) that nobody tries to write
   anything to the SeStringWriter that it is writing to.
*/
class SeBitWriter {
  SeStringWriter *m_writer;
  uchar m_offset;

public:
  SeBitWriter(const SeBitWriter &) = delete;
  SeBitWriter &operator=(const SeBitWriter &) = delete;

  explicit SeBitWriter(SeStringWriter *writer_arg)
      : m_writer(writer_arg), m_offset(0) {}

  void write(uint size, const uint &value) {
    assert((value & ((1 << size) - 1)) == value);

    while (size > 0) {
      if (m_offset == 0) {
        m_writer->write_uint8(0);
      }
      // number of bits to put in this byte
      const uint bits = std::min(size, (uint)(8 - m_offset));
      uchar *const last_byte =
          m_writer->ptr() + m_writer->get_current_pos() - 1;
      *last_byte |= (uchar)((value >> (size - bits)) & ((1 << bits) - 1))
                    << m_offset;
      size -= bits;
      m_offset = (m_offset + bits) & 0x7;
    }
  }
};

class SeBitReader {
  const uchar *m_cur;
  uchar m_offset;
  uint m_ret;
  SeStringReader *const m_reader;

public:
  SeBitReader(const SeBitReader &) = delete;
  SeBitReader &operator=(const SeBitReader &) = delete;

  explicit SeBitReader(SeStringReader *const reader)
      : m_cur(nullptr), m_offset(0), m_reader(reader) {}

  // Returns a pointer to an uint containing the bits read. On subsequent
  // reads, the value being pointed to will be overwritten.  Returns nullptr
  // on failure.
  uint *read(uint size) {
    m_ret = 0;
    assert(size <= 32);

    while (size > 0) {
      if (m_offset == 0) {
        m_cur = (const uchar *)m_reader->read(1);
        if (m_cur == nullptr) {
          return nullptr;
        }
      }
      // how many bits from the current byte?
      const uint bits = std::min((uint)(8 - m_offset), size);
      m_ret <<= bits;
      m_ret |= (*m_cur >> m_offset) & ((1 << bits) - 1);
      size -= bits;
      m_offset = (m_offset + bits) & 0x7;
    }

    return &m_ret;
  }
};

} //namespace smartengine
