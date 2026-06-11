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
#include "se_binlog_manager.h"
#include "dict/se_ddl_manager.h"
#include "dict/se_dict_manager.h"
#include "dict/se_table.h"
#include "handler/se_hton.h"
#include "write_batch/write_batch.h"

namespace smartengine {

/*
  SeBinlogManager class implementation
*/

bool SeBinlogManager::init(SeDictionaryManager *const dict_arg)
{
  assert(dict_arg != nullptr);
  m_dict = dict_arg;

  se_netbuf_store_index(m_key_buf, SeKeyDef::BINLOG_INFO_INDEX_NUMBER);
  m_key_slice = common::Slice(reinterpret_cast<char *>(m_key_buf),
                               SeKeyDef::INDEX_NUMBER_SIZE);
  return false;
}

void SeBinlogManager::cleanup() {}

/**
  Set binlog name, pos and optionally gtid into WriteBatch.
  This function should be called as part of transaction commit,
  since binlog info is set only at transaction commit.
  Actual write into SE is not done here, so checking if
  write succeeded or not is not possible here.
  @param binlog_name   Binlog name
  @param binlog_pos    Binlog pos
  @param binlog_gtid   Binlog max GTID
  @param batch         WriteBatch
*/
void SeBinlogManager::update(const char *const binlog_name,
                             const my_off_t binlog_pos,
                             const char *const binlog_max_gtid,
                             db::WriteBatchBase *const batch)
{
  if (binlog_name && binlog_pos) {
    // max binlog length (512) + binlog pos (4) + binlog gtid (57) < 1024
    const size_t SE_MAX_BINLOG_INFO_LEN = 1024;
    uchar value_buf[SE_MAX_BINLOG_INFO_LEN];
    m_dict->put_key(
        batch, m_key_slice,
        pack_value(value_buf, binlog_name, binlog_pos, binlog_max_gtid));
  }
}

/**
  Read binlog committed entry stored in SE, then unpack
  @param[OUT] binlog_name  Binlog name
  @param[OUT] binlog_pos   Binlog pos
  @param[OUT] binlog_gtid  Binlog GTID
  @return
    true is binlog info was found (valid behavior)
    false otherwise
*/
bool SeBinlogManager::read(char *const binlog_name,
                           my_off_t *const binlog_pos,
                           char *const binlog_gtid) const
{
  bool ret = false;
  if (binlog_name) {
    std::string value;
    common::Status status = m_dict->get_value(m_key_slice, &value);
    if (status.ok()) {
      if (!unpack_value((const uchar *)value.c_str(), binlog_name, binlog_pos, binlog_gtid))
        ret = true;
    }
  }
  return ret;
}

/**
  Pack binlog_name, binlog_pos, binlog_gtid into preallocated
  buffer, then converting and returning a SE Slice
  @param buf           Preallocated buffer to set binlog info.
  @param binlog_name   Binlog name
  @param binlog_pos    Binlog pos
  @param binlog_gtid   Binlog GTID
  @return              common::Slice converted from buf and its length
*/
common::Slice SeBinlogManager::pack_value(uchar *const buf,
                                          const char *const binlog_name,
                                          const my_off_t &binlog_pos,
                                          const char *const binlog_gtid) const
{
  uint pack_len = 0;

  // store version
  se_netbuf_store_uint16(buf, SeKeyDef::BINLOG_INFO_INDEX_NUMBER_VERSION);
  pack_len += SeKeyDef::VERSION_SIZE;

  // store binlog file name length
  assert(strlen(binlog_name) <= FN_REFLEN);
  const uint16_t binlog_name_len = strlen(binlog_name);
  se_netbuf_store_uint16(buf + pack_len, binlog_name_len);
  pack_len += sizeof(uint16);

  // store binlog file name
  memcpy(buf + pack_len, binlog_name, binlog_name_len);
  pack_len += binlog_name_len;

  // store binlog pos
  se_netbuf_store_uint32(buf + pack_len, binlog_pos);
  pack_len += sizeof(uint32);

  // store binlog gtid length.
  // If gtid was not set, store 0 instead
  const uint16_t binlog_gtid_len = binlog_gtid ? strlen(binlog_gtid) : 0;
  se_netbuf_store_uint16(buf + pack_len, binlog_gtid_len);
  pack_len += sizeof(uint16);

  if (binlog_gtid_len > 0) {
    // store binlog gtid
    memcpy(buf + pack_len, binlog_gtid, binlog_gtid_len);
    pack_len += binlog_gtid_len;
  }

  return common::Slice((char *)buf, pack_len);
}

/**
  Unpack value then split into binlog_name, binlog_pos (and binlog_gtid)
  @param[IN]  value        Binlog state info fetched from SE
  @param[OUT] binlog_name  Binlog name
  @param[OUT] binlog_pos   Binlog pos
  @param[OUT] binlog_gtid  Binlog GTID
  @return     true on error
*/
bool SeBinlogManager::unpack_value(const uchar *const value,
                                   char *const binlog_name,
                                   my_off_t *const binlog_pos,
                                   char *const binlog_gtid) const
{
  uint pack_len = 0;

  assert(binlog_pos != nullptr);

  // read version
  const uint16_t version = se_netbuf_to_uint16(value);
  pack_len += SeKeyDef::VERSION_SIZE;
  if (version != SeKeyDef::BINLOG_INFO_INDEX_NUMBER_VERSION)
    return true;

  // read binlog file name length
  const uint16_t binlog_name_len = se_netbuf_to_uint16(value + pack_len);
  pack_len += sizeof(uint16);
  if (binlog_name_len) {
    // read and set binlog name
    memcpy(binlog_name, value + pack_len, binlog_name_len);
    binlog_name[binlog_name_len] = '\0';
    pack_len += binlog_name_len;

    // read and set binlog pos
    *binlog_pos = se_netbuf_to_uint32(value + pack_len);
    pack_len += sizeof(uint32);

    // read gtid length
    const uint16_t binlog_gtid_len = se_netbuf_to_uint16(value + pack_len);
    pack_len += sizeof(uint16);
    if (binlog_gtid && binlog_gtid_len > 0) {
      // read and set gtid
      memcpy(binlog_gtid, value + pack_len, binlog_gtid_len);
      binlog_gtid[binlog_gtid_len] = '\0';
      pack_len += binlog_gtid_len;
    }
  }
  return false;
}

/**
  Inserts a row into mysql.slave_gtid_info table. Doing this inside
  storage engine is more efficient than inserting/updating through MySQL.

  @param[IN] id Primary key of the table.
  @param[IN] db Database name. This is column 2 of the table.
  @param[IN] gtid Gtid in human readable form. This is column 3 of the table.
  @param[IN] write_batch Handle to storage engine writer.
*/
void SeBinlogManager::update_slave_gtid_info(const uint &id,
                                             const char *const db,
                                             const char *const gtid,
                                             db::WriteBatchBase *const write_batch)
{
  if (id && db && gtid) {
     std::shared_ptr<SeTableDef> slave_gtid_info;
    // Make sure that if the slave_gtid_info table exists we have a
    // pointer to it via m_slave_gtid_info_tbl.
    if (!m_slave_gtid_info_tbl.load()) {
      bool from_dict = false;
      slave_gtid_info = se_get_ddl_manager()->find("mysql.slave_gtid_info", &from_dict);
      if (slave_gtid_info && from_dict)
        se_get_ddl_manager()->put(slave_gtid_info);
      m_slave_gtid_info_tbl.store(slave_gtid_info.get());
    }
    if (!m_slave_gtid_info_tbl.load()) {
      // slave_gtid_info table is not present. Simply return.
      return;
    }
    assert(m_slave_gtid_info_tbl.load()->m_key_count == 1);

    const std::shared_ptr<const SeKeyDef> &kd =
        m_slave_gtid_info_tbl.load()->m_key_descr_arr[0];
    String value;

    // Build key
    uchar key_buf[SeKeyDef::INDEX_NUMBER_SIZE + 4] = {0};
    uchar *buf = key_buf;
    se_netbuf_store_index(buf, kd->get_index_number());
    buf += SeKeyDef::INDEX_NUMBER_SIZE;
    se_netbuf_store_uint32(buf, id);
    buf += 4;
    const common::Slice key_slice =
        common::Slice((const char *)key_buf, buf - key_buf);

    // Build value
    uchar value_buf[128] = {0};
    assert(gtid);
    const uint db_len = strlen(db);
    const uint gtid_len = strlen(gtid);
    buf = value_buf;
    // 1 byte used for flags. Empty here.
    buf++;

    // Write column 1.
    assert(strlen(db) <= 64);
    se_netbuf_store_byte(buf, db_len);
    buf++;
    memcpy(buf, db, db_len);
    buf += db_len;

    // Write column 2.
    assert(gtid_len <= 56);
    se_netbuf_store_byte(buf, gtid_len);
    buf++;
    memcpy(buf, gtid, gtid_len);
    buf += gtid_len;
    const common::Slice value_slice =
        common::Slice((const char *)value_buf, buf - value_buf);

    write_batch->Put(kd->get_cf(), key_slice, value_slice);
  }
}

} //namespace smartengine
