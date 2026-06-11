/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012, Monty Program Ab

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

#ifndef SMARTENGINE_TRANSACTION_LIST_WALKER_H_
#define SMARTENGINE_TRANSACTION_LIST_WALKER_H_

#include "se_transaction.h"
namespace smartengine
{

class SeTransListWalker {
public:
  virtual ~SeTransListWalker() {}

  virtual void process_trans(const SeTransaction *const) = 0;
};

class SeSnapshotStatus : public SeTransListWalker {
public:
  SeSnapshotStatus() : m_data(get_header()) {}

  std::string getResult() { return m_data + get_footer(); }

  /* Implement SeTransaction interface */
  /* Create one row in the snapshot status table */
  virtual void process_trans(const SeTransaction *const tx) override;

private:
  static std::string current_timestamp();

  static std::string get_header();

  static std::string get_footer();

private:
  std::string m_data;
};

/*
 * class for exporting transaction information for
 * information_schema.SMARTENGINE_TRX
 */
struct SeTrxInfo {
  std::string name;
  ulonglong trx_id;
  ulonglong write_count;
  ulonglong lock_count;
  int timeout_sec;
  std::string state;
  std::string waiting_key;
  ulonglong waiting_cf_id;
  int is_replication;
  int skip_trx_api;
  int read_only;
  int deadlock_detect;
  int num_ongoing_bulk_load;
  ulong thread_id;
  std::string query_str;
};

/**
 * @brief
 * walks through all non-replication transactions and copies
 * out relevant information for information_schema.se_trx
 */
class SeTrxInfoAggregator : public SeTransListWalker {
public:
  explicit SeTrxInfoAggregator(std::vector<SeTrxInfo> *const trx_info)
      : m_trx_info(trx_info) {}

  virtual void process_trans(const SeTransaction *const tx) override;

private:
  std::vector<SeTrxInfo> *m_trx_info;
};

} // namespace smartengine

#endif // end of SMARTENGINE_TRANSACTION_LIST_WALKER_H_
