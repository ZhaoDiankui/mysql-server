/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 * Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
*/

#pragma once

#include <cstdint>
#include <string>

namespace smartengine
{
namespace util
{

class StackTrace
{
public:
  /** Return a simple stack trace which includes the stack address.
  The function generally executes within a few milliseconds.*/
  static std::string backtrace_fast();
  /** Return a readable stack trace which includes the function names.
  The function generally executes tens of milliseconds.*/
  static std::string backtrace_slow();
  /**Install a signal handler to print callstack on the following signals:
  SIGILL SIGSEGV SIGBUS SIGABRT*/
  static void install_handler();

private:
  static const int64_t MAX_FRAME_DEEPTH = 100;

  static void demangle_symbol(char *symbol, std::ostringstream &backtrace_str);
  static void handler(int signal_num);
};

} // namespace util
} // namespace smartengine
