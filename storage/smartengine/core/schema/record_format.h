/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

namespace smartengine
{
namespace schema
{
/** smartengine record storage format:
+----------+----------+----------+------------------+-----------+----------------+----------------+-----------+------------+
|memcmp key| rec hdr  |field num |null-bitmap length|null-bitmap|unpack data flag|unpack data size|unpack data|non-pk field|
| (varlen) | (1 byte) |(2 bytes) |    (2 bytes)     | (varlen)  |    (1 byte)    |   (2 bytes)    | (varlen)  |  (varlen)  |
|          |          |(optional)|    (optional)    | (optional)|   (optional)   |   (optional)   | (optional)|            |
+----------+----------+----------+------------------+-----------+----------------+----------------+-----------+------------+
mandatory part: memcmp key(comparable primary fields), record header, non-primary key fields.
optional part(instant ddl): field number, null-bitmap length, null-bitmap.
optional part(unpack data): unpack data flag, unpack data size, unpack data.
*/
struct RecordFormat
{
  /**byte size of record header*/
  static const int64_t RECORD_HEADER_SIZE = 1;
  /**byte size of field number*/
  static const int64_t RECORD_FIELD_NUMBER_SIZE = 2;
  /**byte size of nullable field bitmap*/
  static const int64_t RECORD_NULL_BITMAP_SIZE_BYTES = 2;
  /**instant ddl flag*/
  static const uint8_t RECORD_INSTANT_DDL_FLAG = 0x80;
  /**unpack data flag*/
  static const uint8_t RECORD_UNPACK_DATA_FLAG = 0x02;
  /**unpack data flag size*/
  static const int64_t RECORD_UNPACK_DATA_FLAG_SIZE = 1;
  /**byte size of unpack data size*/
  static const int64_t RECORD_UNPACK_DATA_SIZE_BYTES = 2;
  /**byte size of unpack header, include "unpack data flag" and "unpack data size"*/
  static const int64_t RECORD_UNPACK_HEADER_SIZE = RECORD_UNPACK_DATA_FLAG_SIZE + RECORD_UNPACK_DATA_SIZE_BYTES;
  /**byte size of mysql string type length*/
  static const int64_t MYSQL_STRING_SIZE_BYTES = 2;
};

} // namespace schema
} // namespace smartengine