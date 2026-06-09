/*
   Portions Copyright (c) 2024, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2018, 2021, Alibaba and/or its affiliates.
   Portions Copyright (c) 2009, 2023, Oracle and/or its affiliates.

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

/**
  @addtogroup Replication
  @{

  @file control_consensus_events.h
*/

#ifndef CONTROL_CONSENSUS_EVENT_INCLUDED
#define CONTROL_CONSENSUS_EVENT_INCLUDED

#include <sys/types.h>
#include <time.h>
#include <list>
#include <map>
#include <vector>

#include "binlog_event.h"
#include "template_utils.h"
#include "uuid.h"

namespace binary_log {

class Consensus_event : public Binary_log_event {
 public:
  Consensus_event(const char *buf, unsigned int event_len,
                  const Format_description_event *desciption_event);
  /**
  Constructor.
  */
  explicit Consensus_event(unsigned int flag_arg,
                           unsigned long long int term_arg,
                           unsigned long long int index_arg,
                           unsigned long long int length_arg)
      : Binary_log_event(CONSENSUS_LOG_EVENT),
        flag(flag_arg),
        term(term_arg),
        index(index_arg),
        length(length_arg),
        reserve(0) {}

 protected:
  static const int ENCODED_FLAG_LENGTH = 4;
  static const int ENCODED_TERM_LENGTH = 8;
  static const int ENCODED_INDEX_LENGTH = 8;
  static const int ENCODED_LENGTH_LENGTH = 8;
  static const int ENCODED_RESERVE_LENGTH = 8;

  unsigned int flag;             /** preserved */
  unsigned long long int term;   /** term when entry was received by leader */
  unsigned long long int index;  /** position if entry in the log */
  unsigned long long int length; /** log length */

  /* currently, reserve is used for checksum */
  unsigned long long int reserve; /** reserved  */

 public:
  static const int POST_HEADER_LENGTH = ENCODED_FLAG_LENGTH
    + ENCODED_TERM_LENGTH
    + ENCODED_INDEX_LENGTH
    + ENCODED_LENGTH_LENGTH
    + ENCODED_RESERVE_LENGTH;
  static const int CONSENSUS_INDEX_OFFSET = LOG_EVENT_HEADER_LEN
    + ENCODED_FLAG_LENGTH + ENCODED_TERM_LENGTH;

  static const int MAX_EVENT_LENGTH =
    LOG_EVENT_HEADER_LEN + POST_HEADER_LENGTH;
};

class Previous_consensus_index_event : public Binary_log_event {
 public:
  Previous_consensus_index_event(
      const char *buf, unsigned int event_len,
      const Format_description_event *desciption_event);
  /**
  Constructor.
  */
  explicit Previous_consensus_index_event(unsigned long long int index_arg)
      : Binary_log_event(PREVIOUS_CONSENSUS_INDEX_LOG_EVENT),
        index(index_arg) {}

 protected:
  static const int ENCODED_INDEX_LENGTH = 8;

  unsigned long long int
      index; /** max consensus log index in the previous binlog file */

 public:
  static const int POST_HEADER_LENGTH = ENCODED_INDEX_LENGTH;
};

class Consensus_cluster_info_event : public Binary_log_event {
 public:
  Consensus_cluster_info_event(
      const char *buf, unsigned int event_len,
      const Format_description_event *desciption_event);
  /**
  Constructor.
  */
  explicit Consensus_cluster_info_event(unsigned int info_length_arg,
                                        char *info_arg)
      : Binary_log_event(CONSENSUS_CLUSTER_INFO_EVENT),
        info_length(info_length_arg),
        info(info_arg) {}

 protected:
  static const int ENCODED_INFO_LENGTH_LENGTH = 4;

  unsigned int info_length;               /** info length */
  const char *info;                       /** info content */

 public:
  static const int POST_HEADER_LENGTH = ENCODED_INFO_LENGTH_LENGTH;
};

class Consensus_empty_event : public Binary_log_event {
 public:
  Consensus_empty_event(const char *buf, unsigned int event_len,
                        const Format_description_event *desciption_event);
  /**
  Constructor.
  */
  explicit Consensus_empty_event() : Binary_log_event(CONSENSUS_EMPTY_EVENT) {}
};

}  // namespace binary_log

#endif
