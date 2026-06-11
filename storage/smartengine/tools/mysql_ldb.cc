//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#include "se_comparator.h"
#include "tools/ldb_tool.h"

int main(int argc, char **argv) {
  smartengine::common::Options db_options;
  const smartengine::SePrimaryKeyComparator pk_comparator;
  db_options.comparator = &pk_comparator;

  smartengine::tools::LDBTool tool;
  tool.Run(argc, argv, db_options);
  return 0;
}
