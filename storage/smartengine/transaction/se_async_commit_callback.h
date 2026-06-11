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

#ifndef SMARTENGINE_TRANSACTION_ASYNC_COMMIT_CALLBACK_H_
#define SMARTENGINE_TRANSACTION_ASYNC_COMMIT_CALLBACK_H_

#include "sql_base.h"
#include "db/async_callback.h"

namespace smartengine
{

class SeAsyncCommitCallback : public common::AsyncCallback {
public:
  SeAsyncCommitCallback() : AsyncCallback(false)
  {
    reset();
  }

  ~SeAsyncCommitCallback() override {}

  common::Status call_back() override {
    assert(nullptr != this->thd_);
    //not support async commit now in 8.0
    abort();
    //async_commit_direct(this->thd_);
    return common::Status::OK();
  }

  void reset() {
    this->thd_ = nullptr;
  }

  void reset_thd(THD* thd) {
    assert(nullptr != thd);
    this->thd_ = thd;
  }

  bool destroy_on_complete() override{ return false; }
private:
  THD*                             thd_;
};

} // namespace smartengine

#endif // end of SMARTENGINE_TRANSACTION_ASYNC_COMMIT_CALLBACK_H_
