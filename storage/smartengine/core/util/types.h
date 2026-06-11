/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <stdint.h>
#include "port/port_posix.h"

namespace smartengine
{
namespace common
{

// Represents a sequence number in a WAL file.
typedef uint64_t SequenceNumber;

static const common::SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);

static const common::SequenceNumber kDisableGlobalSequenceNumber = port::kMaxUint64;

}  // namespace common
}  // namespace smartengine