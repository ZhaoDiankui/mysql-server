// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "io/easy_log.h"

extern easy_log_level_t easy_log_level;
easy_log_level_t save_easy_log_level;

TEST(easy, enableDebugLog) {
  save_easy_log_level = easy_log_level;
  easy_log_level = EASY_LOG_DEBUG;
}

TEST(easy, debugLog) { easy_debug_log("debug log"); }

TEST(easy, disableDebugLog) { easy_log_level = save_easy_log_level; }
