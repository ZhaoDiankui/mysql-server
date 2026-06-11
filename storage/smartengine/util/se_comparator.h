/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012,2015 Monty Program Ab

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

/* C++ system header files */
#include <string>

/* se header files */
#include "util/comparator.h"

/* MyX header files */
#include "./se_utils.h"

namespace smartengine {

/*
  The keys are in form: {index_number} {mem-comparable-key}

  (todo: knowledge about this format is shared between this class and
   SeKeyDef)
*/
class SePrimaryKeyComparator : public smartengine::util::Comparator {
public:
  SePrimaryKeyComparator(const SePrimaryKeyComparator &) = delete;
  SePrimaryKeyComparator &operator=(const SePrimaryKeyComparator &) = delete;
  SePrimaryKeyComparator() = default;

  static int bytewise_compare(const smartengine::common::Slice &a,
                              const smartengine::common::Slice &b) {
    const size_t a_size = a.size();
    const size_t b_size = b.size();
    const size_t len = (a_size < b_size) ? a_size : b_size;
    int res;

    if ((res = memcmp(a.data(), b.data(), len)))
      return res;

    /* Ok, res== 0 */
    if (a_size != b_size) {
      return a_size < b_size ? -1 : 1;
    }
    return HA_EXIT_SUCCESS;
  }

  /* Override virtual methods of interest */

  int Compare(const smartengine::common::Slice &a, const smartengine::common::Slice &b) const override {
    return bytewise_compare(a, b);
  }

  const char *Name() const override { return "SmartEngine_SE_V1.0"; }

  // TODO: advanced funcs:
  // - FindShortestSeparator
  // - FindShortSuccessor

  // for now, do-nothing implementations:
  void FindShortestSeparator(std::string *start,
                             const smartengine::common::Slice &limit) const override {}
  void FindShortSuccessor(std::string *key) const override {}
};

class SePrimaryKeyReverseComparator : public smartengine::util::Comparator {
public:
  SePrimaryKeyReverseComparator(const SePrimaryKeyReverseComparator &) = delete;
  SePrimaryKeyReverseComparator &operator=(const SePrimaryKeyReverseComparator &) = delete;
  SePrimaryKeyReverseComparator() = default;

  static int bytewise_compare(const smartengine::common::Slice &a,
                              const smartengine::common::Slice &b) {
    return -SePrimaryKeyComparator::bytewise_compare(a, b);
  }

  int Compare(const smartengine::common::Slice &a, const smartengine::common::Slice &b) const override {
    return -SePrimaryKeyComparator::bytewise_compare(a, b);
  }
  const char *Name() const override { return "rev:SE_SE_v3.10"; }
  void FindShortestSeparator(std::string *start,
                             const smartengine::common::Slice &limit) const override {}
  void FindShortSuccessor(std::string *key) const override {}
};

} //namespace smartengine
