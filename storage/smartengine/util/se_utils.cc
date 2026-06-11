/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2016, Facebook, Inc.

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

#include "se_utils.h"
#include <array>
#include <string>
#include <ctype.h>
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "my_dir.h"
#include "mysqld.h"
#include "dict/se_index.h"
#include "handler/ha_smartengine.h"
#include "plugin/se_system_vars.h"

namespace smartengine
{

/*
  Skip past any spaces in the input
*/
const char *se_skip_spaces(const CHARSET_INFO *const cs, const char *str)
{
  assert(cs != nullptr);
  assert(str != nullptr);

  while (my_isspace(cs, *str)) {
    str++;
  }

  return str;
}

/*
  Compare (ignoring case) to see if str2 is the next data in str1.
  Note that str1 can be longer but we only compare up to the number
  of characters in str2.
*/
bool se_compare_strings_ic(const char *const str1, const char *const str2)
{
  assert(str1 != nullptr);
  assert(str2 != nullptr);

  // Scan through the strings
  size_t ii;
  for (ii = 0; str2[ii]; ii++) {
    if (toupper(static_cast<int>(str1[ii])) !=
        toupper(static_cast<int>(str2[ii]))) {
      return false;
    }
  }

  return true;
}

/*
  Scan through an input string looking for pattern, ignoring case
  and skipping all data enclosed in quotes.
*/
const char *se_find_in_string(const char *str,
                              const char *pattern,
                              bool *const succeeded)
{
  char quote = '\0';
  bool escape = false;

  assert(str != nullptr);
  assert(pattern != nullptr);
  assert(succeeded != nullptr);

  *succeeded = false;

  for (; *str; str++) {
    /* If we found a our starting quote character */
    if (*str == quote) {
      /* If it was escaped ignore it */
      if (escape) {
        escape = false;
      }
      /* Otherwise we are now outside of the quoted string */
      else {
        quote = '\0';
      }
    }
    /* Else if we are currently inside a quoted string? */
    else if (quote != '\0') {
      /* If so, check for the escape character */
      escape = !escape && *str == '\\';
    }
    /* Else if we found a quote we are starting a quoted string */
    else if (*str == '"' || *str == '\'' || *str == '`') {
      quote = *str;
    }
    /* Else we are outside of a quoted string - look for our pattern */
    else {
      if (se_compare_strings_ic(str, pattern)) {
        *succeeded = true;
        return str;
      }
    }
  }

  // Return the character after the found pattern or the null terminateor
  // if the pattern wasn't found.
  return str;
}

/*
  See if the next valid token matches the specified string
*/
const char *se_check_next_token(const CHARSET_INFO *const cs,
                                const char *str,
                                const char *const pattern,
                                bool *const succeeded)
{
  assert(cs != nullptr);
  assert(str != nullptr);
  assert(pattern != nullptr);
  assert(succeeded != nullptr);

  // Move past any spaces
  str = se_skip_spaces(cs, str);

  // See if the next characters match the pattern
  if (se_compare_strings_ic(str, pattern)) {
    *succeeded = true;
    return str + strlen(pattern);
  }

  *succeeded = false;
  return str;
}

/*
  Parse id
*/
const char *se_parse_id(const CHARSET_INFO *const cs,
                        const char *str,
                        std::string *const id)
{
  assert(cs != nullptr);
  assert(str != nullptr);

  // Move past any spaces
  str = se_skip_spaces(cs, str);

  if (*str == '\0') {
    return str;
  }

  char quote = '\0';
  if (*str == '`' || *str == '"') {
    quote = *str++;
  }

  size_t len = 0;
  const char *start = str;

  if (quote != '\0') {
    for (;;) {
      if (*str == '\0') {
        return str;
      }

      if (*str == quote) {
        str++;
        if (*str != quote) {
          break;
        }
      }

      str++;
      len++;
    }
  } else {
    while (!my_isspace(cs, *str) && *str != '(' && *str != ')' && *str != '.' &&
           *str != ',' && *str != '\0') {
      str++;
      len++;
    }
  }

  // If the user requested the id create it and return it
  if (id != nullptr) {
    *id = std::string("");
    id->reserve(len);
    while (len--) {
      *id += *start;
      if (*start++ == quote) {
        start++;
      }
    }
  }

  return str;
}

/*
  Skip id
*/
const char *se_skip_id(const CHARSET_INFO *const cs, const char *str)
{
  assert(cs != nullptr);
  assert(str != nullptr);

  return se_parse_id(cs, str, nullptr);
}

static const std::size_t se_hex_bytes_per_char = 2;
static const std::array<char, 16> se_hexdigit = {{'0', '1', '2', '3', '4', '5',
                                                   '6', '7', '8', '9', 'a', 'b',
                                                   'c', 'd', 'e', 'f'}};

/*
  Convert data into a hex string with optional maximum length.
  If the data is larger than the maximum length trancate it and append "..".
*/
std::string se_hexdump(const char *data,
                       const std::size_t data_len,
                       const std::size_t maxsize)
{
  assert(data != nullptr);

  // Count the elements in the string
  std::size_t elems = data_len;
  // Calculate the amount of output needed
  std::size_t len = elems * se_hex_bytes_per_char;
  std::string str;

  if (maxsize != 0 && len > maxsize) {
    // If the amount of output is too large adjust the settings
    // and leave room for the ".." at the end
    elems = (maxsize - 2) / se_hex_bytes_per_char;
    len = elems * se_hex_bytes_per_char + 2;
  }

  // Reserve sufficient space to avoid reallocations
  str.reserve(len);

  // Loop through the input data and build the output string
  for (std::size_t ii = 0; ii < elems; ii++, data++) {
    uint8_t ch = (uint8_t)*data;
    str += se_hexdigit[ch >> 4];
    str += se_hexdigit[ch & 0x0F];
  }

  // If we can't fit it all add the ".."
  if (elems != data_len) {
    str += "..";
  }

  return str;
}

/*
  Attempt to access the database subdirectory to see if it exists
*/
bool se_database_exists(const std::string &db_name)
{
  const std::string dir =
      std::string(mysql_real_data_home) + FN_DIRSEP + db_name;
  struct MY_DIR *const dir_info =
      my_dir(dir.c_str(), MYF(MY_DONT_SORT | MY_WANT_STAT));
  if (dir_info == nullptr) {
    return false;
  }

  my_dirend(dir_info);
  return true;
}

/*
  Set the patterns string.  If there are invalid regex patterns they will
  be stored in m_bad_patterns and the result will be false, otherwise the
  result will be true.
*/
bool Regex_list_handler::set_patterns(const std::string& pattern_str)
{
  bool pattern_valid= true;

  // Create a normalized version of the pattern string with all delimiters
  // replaced by the '|' character
  std::string norm_pattern= pattern_str;
  std::replace(norm_pattern.begin(), norm_pattern.end(), m_delimiter, '|');

  // Make sure no one else is accessing the list while we are changing it.
  mysql_rwlock_wrlock(&m_rwlock);

  // Clear out any old error information
  m_bad_pattern_str.clear();

  try
  {
    // Replace all delimiters with the '|' operator and create the regex
    // Note that this means the delimiter can not be part of a regular
    // expression.  This is currently not a problem as we are using the comma
    // character as a delimiter and commas are not valid in table names.
    const std::regex* pattern= new std::regex(norm_pattern);

    // Free any existing regex information and setup the new one
    delete m_pattern;
    m_pattern= pattern;
  }
  catch (const std::regex_error& e)
  {
    // This pattern is invalid.
    pattern_valid= false;

    // Put the bad pattern into a member variable so it can be retrieved later.
    m_bad_pattern_str= pattern_str;
  }

  // Release the lock
  mysql_rwlock_unlock(&m_rwlock);

  return pattern_valid;
}

bool Regex_list_handler::matches(const std::string& str) const
{
  assert(m_pattern != nullptr);

  // Make sure no one else changes the list while we are accessing it.
  mysql_rwlock_rdlock(&m_rwlock);

  // See if the table name matches the regex we have created
  bool found= std::regex_match(str, *m_pattern);

  // Release the lock
  mysql_rwlock_unlock(&m_rwlock);

  return found;
}

void warn_about_bad_patterns(const Regex_list_handler* regex_list_handler,
                             const char *name)
{
  // There was some invalid regular expression data in the patterns supplied

  // NO_LINT_DEBUG
  sql_print_warning("Invalid pattern in %s: %s", name,
                    regex_list_handler->bad_pattern().c_str());
}

String timeout_message(const char *command,
                       const char *name1,
                       const char *name2)
{
    String msg; 
    msg.append("Timeout on ");
    msg.append(command);
    msg.append(": ");
    msg.append(name1);
    if (name2 && name2[0])
    {    
      msg.append(".");
      msg.append(name2);
    }    
    return msg; 
}

/**
  @brief
  splits the normalized table name of <dbname>.<tablename>#P#<part_no> into
  the <dbname>, <tablename> and <part_no> components.

  @param dbbuf returns database name/table_schema
  @param tablebuf returns tablename
  @param partitionbuf returns partition suffix if there is one
  @return HA_EXIT_SUCCESS on success, non-zero on failure to split
*/
int se_split_normalized_tablename(const std::string &fullname,
                                   std::string *const db,
                                   std::string *const table,
                                   std::string *const partition)
{
  assert(!fullname.empty());

#define SE_PARTITION_STR "#P#"

  /* Normalize returns dbname.tablename. */
  size_t dotpos = fullname.find('.');

  /* Invalid table name? */
  if (dotpos == std::string::npos) {
    return HA_ERR_INTERNAL_ERROR;
  }

  // Table must have a database name associated with it.
  assert(dotpos > 0);

  if (db != nullptr) {
    *db = fullname.substr(0, dotpos);
  }

  dotpos++;

  const size_t partpos =
      fullname.find(SE_PARTITION_STR, dotpos, strlen(SE_PARTITION_STR));

  if (partpos != std::string::npos) {
    assert(partpos >= dotpos);

    if (table != nullptr) {
      *table = fullname.substr(dotpos, partpos - dotpos);
    }

    if (partition != nullptr) {
      *partition = fullname.substr(partpos + strlen(SE_PARTITION_STR));
    }
  } else if (table != nullptr) {
    *table = fullname.substr(dotpos);
  }

  return HA_EXIT_SUCCESS;
}

const char *get_se_io_error_string(const SE_IO_ERROR_TYPE err_type)
{
  // If this assertion fails then this means that a member has been either added
  // to or removed from SE_IO_ERROR_TYPE enum and this function needs to be
  // changed to return the appropriate value.
  static_assert(SE_IO_ERROR_LAST == 4, "Please handle all the error types.");

  switch (err_type) {
  case SE_IO_ERROR_TYPE::SE_IO_ERROR_TX_COMMIT:
    return "SE_IO_ERROR_TX_COMMIT";
  case SE_IO_ERROR_TYPE::SE_IO_ERROR_DICT_COMMIT:
    return "SE_IO_ERROR_DICT_COMMIT";
  case SE_IO_ERROR_TYPE::SE_IO_ERROR_BG_THREAD:
    return "SE_IO_ERROR_BG_THREAD";
  case SE_IO_ERROR_TYPE::SE_IO_ERROR_GENERAL:
    return "SE_IO_ERROR_GENERAL";
  default:
    assert(false);
    return "(unknown)";
  }
}

// In case of core dump generation we want this function NOT to be optimized
// so that we can capture as much data as possible to debug the root cause
// more efficiently.
#ifdef __GNUC__
#ifndef __clang__
#pragma GCC push_options
#pragma GCC optimize("O0")
#endif //__clang__
#endif //__GNUC__
void se_handle_io_error(const common::Status status, const SE_IO_ERROR_TYPE err_type)
{
  if (status.IsIOError()) {
    switch (err_type) {
    case SE_IO_ERROR_TX_COMMIT:
    case SE_IO_ERROR_DICT_COMMIT: {
      /* NO_LINT_DEBUG */
      sql_print_error("SE: failed to write to WAL. Error type = %s, "
                      "status code = %d, status = %s",
                      get_se_io_error_string(err_type), status.code(),
                      status.ToString().c_str());
      /* NO_LINT_DEBUG */
      sql_print_error("SE: aborting on WAL write error.");
      abort_with_stack_traces();
      break;
    }
    case SE_IO_ERROR_BG_THREAD: {
      /* NO_LINT_DEBUG */
      sql_print_warning("SE: background thread failed to write to SE. "
                        "Error type = %s, status code = %d, status = %s",
                        get_se_io_error_string(err_type), status.code(),
                        status.ToString().c_str());
      break;
    }
    case SE_IO_ERROR_GENERAL: {
      /* NO_LINT_DEBUG */
      sql_print_error("SE: failed on I/O. Error type = %s, "
                      "status code = %d, status = %s",
                      get_se_io_error_string(err_type), status.code(),
                      status.ToString().c_str());
      /* NO_LINT_DEBUG */
      sql_print_error("SE: aborting on I/O error.");
      abort_with_stack_traces();
      break;
    }
    default:
      assert(0);
      break;
    }
  } else if (status.IsCorruption()) {
    /* NO_LINT_DEBUG */
    sql_print_error("SE: data corruption detected! Error type = %s, "
                    "status code = %d, status = %s",
                    get_se_io_error_string(err_type), status.code(),
                    status.ToString().c_str());
    /* NO_LINT_DEBUG */
    sql_print_error("SE: aborting because of data corruption.");
    abort_with_stack_traces();
  } else if (!status.ok()) {
    switch (err_type) {
    case SE_IO_ERROR_DICT_COMMIT: {
      /* NO_LINT_DEBUG */
      sql_print_error("SE: failed to write to WAL (dictionary). "
                      "Error type = %s, status code = %d, status = %s",
                      get_se_io_error_string(err_type), status.code(),
                      status.ToString().c_str());
      /* NO_LINT_DEBUG */
      sql_print_error("SE: aborting on WAL write error.");
      abort_with_stack_traces();
      break;
    }
    default:
      /* NO_LINT_DEBUG */
      sql_print_warning("SE: failed to read/write in SE. "
                        "Error type = %s, status code = %d, status = %s",
                        get_se_io_error_string(err_type), status.code(),
                        status.ToString().c_str());
      break;
    }
  }
}

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC pop_options
#endif //__clang__
#endif //__GNUC__

int se_normalize_tablename(const std::string &tablename, std::string *const strbuf)
{
  assert(strbuf != nullptr);

  if (tablename.size() < 2 || tablename[0] != '.' || tablename[1] != '/') {
    assert(0); // We were not passed table name?
    return HA_ERR_INTERNAL_ERROR;
  }

  size_t pos = tablename.find_first_of('/', 2);
  if (pos == std::string::npos) {
    assert(0); // We were not passed table name?
    return HA_ERR_INTERNAL_ERROR;
  }

  *strbuf = tablename.substr(2, pos - 2) + "." + tablename.substr(pos + 1);

  return HA_EXIT_SUCCESS;
}

/**
  Deciding if it is possible to use bloom filter or not.

  @detail
   Even if bloom filter exists, it is not always possible
   to use bloom filter. If using bloom filter when you shouldn't,
   false negative may happen -- fewer rows than expected may be returned.
   It is users' responsibility to use bloom filter correctly.

   If bloom filter does not exist, return value does not matter because
   se does not use bloom filter internally.

  @param kd
  @param eq_cond      Equal condition part of the key. This always includes
                      system index id (4 bytes).
  @param use_all_keys True if all key parts are set with equal conditions.
                      This is aware of extended keys.
*/
bool can_use_bloom_filter(THD *thd, const SeKeyDef &kd,
                          const common::Slice &eq_cond,
                          const bool use_all_keys,
                          bool is_ascending)
{
  bool can_use = false;

  if (use_all_keys)
    can_use = true;
  else
    can_use = false;

  return can_use;
}

common::WriteOptions se_get_se_write_options(my_core::THD *const thd)
{
  common::WriteOptions opt;

  opt.sync = se_flush_log_at_trx_commit == 1;
  //opt.disableWAL = THDVAR(thd, write_disable_wal);
  opt.disableWAL = se_thd_write_disable_wal(thd);
  opt.ignore_missing_column_families = false;
      // THDVAR(thd, write_ignore_missing_column_families);

  return opt;
}

void print_error_row(const char *const db_name,
                     const char *const table_name,
                     const String &rowkey,
                     const String &sec_key,
                     const std::string &retrieved_record)
{
  std::string buf;
  buf = se_hexdump(rowkey.ptr(), rowkey.length(), SE_MAX_HEXDUMP_LEN);
  // NO_LINT_DEBUG
  sql_print_error("CHECK TABLE %s.%s:   rowkey: %s", db_name, table_name,
                  buf.c_str());

  buf = se_hexdump(retrieved_record.data(), retrieved_record.size(),
                    SE_MAX_HEXDUMP_LEN);
  // NO_LINT_DEBUG
  sql_print_error("CHECK TABLE %s.%s:   record: %s", db_name, table_name,
                  buf.c_str());

  buf = se_hexdump(sec_key.ptr(), sec_key.length(), SE_MAX_HEXDUMP_LEN);
  // NO_LINT_DEBUG
  sql_print_error("CHECK TABLE %s.%s:   index: %s", db_name, table_name,
                  buf.c_str());
}

bool can_hold_read_locks_on_select(THD *thd, thr_lock_type lock_type)
{
  return (lock_type == TL_READ_WITH_SHARED_LOCKS) ||
         (lock_type == TL_READ_NO_INSERT) ||
         (lock_type != TL_IGNORE && thd->lex->sql_command != SQLCOM_SELECT);
}

#ifndef NDEBUG
void dbug_append_garbage_at_end(std::string &on_disk_rec)
{
  on_disk_rec.append("abc");
}

// Truncating the length of the record to 0 may result in undefined behavior in
// 'std::string' operations such as accessing the front, potentially causing a
// crash.
void dbug_truncate_record(std::string &on_disk_rec) { on_disk_rec.resize(1); }

void dbug_modify_rec_varchar12(std::string &on_disk_rec)
{
  std::string res;
  // The record is NULL-byte followed by VARCHAR(10).
  // Put the NULL-byte
  res.append("\0", 1);
  // Then, add a valid VARCHAR(12) value.
  res.append("\xC", 1);
  res.append("123456789ab", 12);

  on_disk_rec.assign(res);
}

void dbug_modify_key_varchar8(String &on_disk_rec)
{
  std::string res;
  // The key starts with index number
  res.append(on_disk_rec.ptr(), SeKeyDef::INDEX_NUMBER_SIZE);

  // Then, a mem-comparable form of a varchar(8) value.
  res.append("ABCDE\0\0\0\xFC", 9);
  on_disk_rec.length(0);
  on_disk_rec.append(res.data(), res.size());
}
#endif

} //namespace smartengine
