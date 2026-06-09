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

#ifndef CONSENSUS_LOG_EVENT_INCLUDE
#define CONSENSUS_LOG_EVENT_INCLUDE
/*****************************************************************************

Consensus Log Event class

The class is not logged to a binary log, and is not applied on to the slave.
The decoding of the event on the slave side is done by its superclass,
binary_log::Heartbeat_event.

****************************************************************************/
class Consensus_log_event : public binary_log::Consensus_event,
                            public Log_event {
 public:
#ifndef MYSQL_CLIENT
  /**
  Create a new event without a THD object.
  */
  Consensus_log_event(uint32 flag_arg, uint64 term_arg, uint64 index_arg,
                      uint64 length_arg);
#endif
  Consensus_log_event(const char *buffer, uint event_len,
                      const Format_description_event *description_event);

#ifdef MYSQL_SERVER
  bool write_data_header(Basic_ostream *ostream) override;
  uint32 write_data_header_to_memory(uchar *buffer);
#endif
  // Used internally by both print() and pack_info().
  size_t to_string(char *buffer) const;

#ifdef MYSQL_SERVER
  int pack_info(Protocol *) override;
#else
  void print(FILE *file, PRINT_EVENT_INFO *print_event_info) const override;
#endif

  size_t get_data_size() override { return POST_HEADER_LENGTH; }

  static size_t get_event_length() {
    return LOG_EVENT_HEADER_LEN + POST_HEADER_LENGTH;
  }

#if defined(MYSQL_SERVER)
  enum_skip_reason do_shall_skip(Relay_log_info *rli
                                 [[maybe_unused]]) override {
    return EVENT_SKIP_IGNORE;
  }
  int do_apply_event(Relay_log_info const *rli) override;
  int do_update_pos(Relay_log_info *rli) override;
#endif

#ifdef MYSQL_SERVER
  /**
  Writes this event to a memory buffer.

  @param buf The event will be written to this buffer.

  @return the number of bytes written, i.e., always
  LOG_EVENT_HEADER_LEN + Gtid_log_event::POST_HEADEr_LENGTH.
  */
  uint32 write_to_memory(uchar *buf) {
    common_header->data_written = LOG_EVENT_HEADER_LEN + get_data_size();
    uint32 len = write_header_to_memory(buf);
    len += write_data_header_to_memory(buf + len);
    return len;
  }
#endif

  uint64 get_flag() const { return flag; }
  uint64 get_term() const { return term; }
  uint64 get_index() const { return index; }
  uint64 get_length() const { return length; }
  uint64 get_reserve() const { return reserve; }
  void set_reserve(uint64 arg) { reserve = arg; }

  static const char *SET_STRING_FLAG;
  static const char *SET_STRING_TERM;
  static const char *SET_STRING_INDEX;
  static const char *SET_STRING_LENGTH;
  static const char *SET_STRING_RESERVE;

 private:
  static const size_t SET_STRING_FLAG_LENGTH = 18;
  static const size_t SET_STRING_TERM_LENGTH = 7;
  static const size_t SET_STRING_INDEX_LENGTH = 8;
  static const size_t SET_STRING_LENGTH_LENGTH = 9;
  static const size_t SET_STRING_RESERVE_LENGTH = 10;

  static const size_t MAX_SET_STRING_LENGTH =
      SET_STRING_FLAG_LENGTH + SET_STRING_TERM_LENGTH +
      SET_STRING_INDEX_LENGTH + SET_STRING_LENGTH_LENGTH +
      SET_STRING_RESERVE_LENGTH + 18U + 36U + 36U + 36U + 36U;
};

/*****************************************************************************

Previous Consensus Index Log Event class

The class is not logged to a binary log, and is not applied on to the slave.
The decoding of the event on the slave side is done by its superclass,
binary_log::Heartbeat_event.

****************************************************************************/
class Previous_consensus_index_log_event
    : public binary_log::Previous_consensus_index_event,
      public Log_event {
 public:
#ifndef MYSQL_CLIENT
  /**
  Create a new event without a THD object.
  */
  Previous_consensus_index_log_event(uint64 index_arg);
#endif
  Previous_consensus_index_log_event(
      const char *buffer, uint event_len,
      const Format_description_event *description_event);

#ifdef MYSQL_SERVER
  bool write_data_header(Basic_ostream *ostream) override;
  uint32 write_data_header_to_memory(uchar *buffer);
#endif
  // Used internally by both print() and pack_info().
  size_t to_string(char *buffer) const;

#ifdef MYSQL_SERVER
  int pack_info(Protocol *) override;
#else
  void print(FILE *file, PRINT_EVENT_INFO *print_event_info) const override;
#endif

  size_t get_data_size() override { return POST_HEADER_LENGTH; }

#ifdef MYSQL_SERVER
  enum_skip_reason do_shall_skip(Relay_log_info *rli
                                 [[maybe_unused]]) override {
    return EVENT_SKIP_IGNORE;
  }
  int do_apply_event(Relay_log_info const *rli) override;
  int do_update_pos(Relay_log_info *rli) override;
#endif

  uint64 get_index() const { return index; }

  static const char *SET_STRING_PREFIX;

 private:
  static const size_t SET_STRING_PREFIX_LENGTH = 24;

  static const size_t MAX_SET_STRING_LENGTH = SET_STRING_PREFIX_LENGTH + 36U;
};

/*****************************************************************************

Consensus Log Event class

The class is not logged to a binary log, and is not applied on to the slave.
The decoding of the event on the slave side is done by its superclass,
binary_log::Heartbeat_event.

****************************************************************************/
class Consensus_cluster_info_log_event
    : public binary_log::Consensus_cluster_info_event,
      public Log_event {
 public:
#ifndef MYSQL_CLIENT
  /**
  Create a new event without a THD object.
  */
  Consensus_cluster_info_log_event(uint32 info_length_arg, char *info_arg);
#endif
  Consensus_cluster_info_log_event(
      const char *buffer, uint event_len,
      const Format_description_event *description_event);

#ifdef MYSQL_SERVER
  bool write_data_header(Basic_ostream *ostream) override;
  uint32 write_data_header_to_memory(uchar *buffer);
#endif
  // Used internally by both print() and pack_info().
  size_t to_string(char *buffer) const;

#ifdef MYSQL_SERVER
  int pack_info(Protocol *) override;
#else
  void print(FILE *file, PRINT_EVENT_INFO *print_event_info) const override;
#endif

  size_t get_data_size() override { return POST_HEADER_LENGTH + info_length; }

  size_t get_event_length() { return LOG_EVENT_HEADER_LEN + get_data_size(); }

#ifdef MYSQL_SERVER
  enum_skip_reason do_shall_skip(Relay_log_info *rli
                                 [[maybe_unused]]) override {
    return EVENT_SKIP_IGNORE;
  }
  int do_apply_event(Relay_log_info const *rli) override;
  int do_update_pos(Relay_log_info *rli) override;
#endif

#ifdef MYSQL_SERVER
  /**
  Writes this event to a memory buffer.

  @param buf The event will be written to this buffer.

  @return the number of bytes written, i.e., always
  LOG_EVENT_HEADER_LEN + Gtid_log_event::POST_HEADEr_LENGTH.
  */
  uint32 write_to_memory(uchar *buf) {
    common_header->data_written = LOG_EVENT_HEADER_LEN + get_data_size();
    uint32 len = write_header_to_memory(buf);
    len += write_data_header_to_memory(buf + len);
    return len;
  }
#endif
  uint32 get_info_length() { return info_length; }
  const char *get_info() { return info; }

  static const char *SET_STRING_INFO_LENGTH;

 private:
  static const size_t SET_STRING_INFO_LENGTH_LENGTH = 33;

  static const size_t MAX_SET_STRING_LENGTH =
      SET_STRING_INFO_LENGTH_LENGTH + 36U;
};

class Consensus_empty_log_event : public binary_log::Consensus_empty_event,
                                  public Log_event {
 public:
#ifndef MYSQL_CLIENT
  /**
  Create a new event without a THD object.
  */
  Consensus_empty_log_event();
#endif
  Consensus_empty_log_event(const char *buffer, uint event_len,
                            const Format_description_event *description_event);

  // Used internally by both print() and pack_info().
  size_t to_string(char *buffer) const;

#ifdef MYSQL_SERVER
  int pack_info(Protocol *) override;
#else
  void print(FILE *file, PRINT_EVENT_INFO *print_event_info) const override;
#endif

  size_t get_data_size() override { return 0; }

  static size_t get_event_length() { return LOG_EVENT_HEADER_LEN; }

#ifdef MYSQL_SERVER
  enum_skip_reason do_shall_skip(Relay_log_info *rli
                                 [[maybe_unused]]) override {
    return EVENT_SKIP_IGNORE;
  }
  int do_apply_event(Relay_log_info const *rli) override;
  int do_update_pos(Relay_log_info *rli) override;
#endif

  static const char *SET_STRING;

 private:
  static const size_t SET_STRING_LENGTH = 17;

  static const size_t MAX_SET_STRING_LENGTH = SET_STRING_LENGTH;
};
#endif /* _log_event_h */
