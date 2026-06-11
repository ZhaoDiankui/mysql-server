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

#include "schema/column_schema.h"

namespace smartengine
{
namespace table
{

struct ColumnParseCtx;

class Column
{
public:
  Column();
  Column(const schema::ColumnSchema &schema);
  virtual ~Column();

  virtual void reuse();
  virtual bool is_valid() const;
  virtual int parse(ColumnParseCtx &parse_ctx);
  virtual void assign(const common::Slice &buf);
  virtual bool is_null() const { return false; }
  virtual bool is_primary() const { return schema_.is_primary(); }
  virtual char *buf() { return buf_; }
  virtual const char *buf() const { return buf_; }
  virtual int64_t size() const { return buf_size_; }
  virtual uint8_t type() const { return schema_.get_type(); }

  DECLARE_VIRTUAL_TO_STRING()
protected:
  virtual int do_parse(ColumnParseCtx &parse_ctx);
  int parse_bytes(int64_t bytes,
                  char *buf,
                  int64_t buf_size,
                  int64_t &pos,
                  int64_t &value);
public:
  schema::ColumnSchema schema_;
  char *buf_;
  int64_t buf_size_;
};
typedef std::vector<Column *> ColumnArray;

class RecordHeader : public Column
{
public:
  RecordHeader();
  RecordHeader(const schema::ColumnSchema &schema);
  virtual ~RecordHeader() override;

  virtual void reuse() override;
  virtual bool is_valid() const override;
  DECLARE_OVERRIDE_TO_STRING()
private:
  virtual int do_parse(ColumnParseCtx &parse_ctx) override;
public:
  uint8_t flag_;
  int64_t col_cnt_;
  int64_t null_bitmap_size_;
};

class NullBitmap : public Column
{
public:
  NullBitmap();
  NullBitmap(const schema::ColumnSchema &schema);
  virtual ~NullBitmap() override;

  virtual void reuse() override;
  virtual bool is_valid() const override;
  bool check_null(uint32_t null_offset, uint8_t null_mask) const;
  DECLARE_OVERRIDE_TO_STRING()
private:
  virtual int do_parse(ColumnParseCtx &parse_ctx) override;

private:
  int64_t null_bitmap_size_;
  char *null_bitmap_;
};

class UnpackInfo : public Column
{
public:
  UnpackInfo();
  UnpackInfo(const schema::ColumnSchema &schema);
  virtual ~UnpackInfo() override;

  virtual void reuse() override;
  virtual bool is_valid() const override;
  DECLARE_OVERRIDE_TO_STRING()
protected:
  virtual int do_parse(ColumnParseCtx &parse_ctx) override;

  int parse_unpack_info_size(char *buf, int64_t buf_size, int64_t &pos);
private:
  uint8_t flag_;
  int64_t unpack_info_size_;
  char *unpack_info_;
};

class DataColumn : public Column
{
public:
  DataColumn();
  DataColumn(const schema::ColumnSchema &schema);
  virtual ~DataColumn() override;

  virtual void reuse() override;
  virtual void assign(const common::Slice &buf) override;
  virtual bool is_valid() const override;
  virtual bool is_null() const override { return is_null_; }
  DECLARE_OVERRIDE_TO_STRING()
protected:
  virtual int do_parse(ColumnParseCtx &parse_ctx) override;

private:
  int parse_column_data_size(char *buf,
                             int64_t buf_size,
                             int64_t &pos);
private:
  bool is_null_;
  int64_t data_size_;
  char *data_;
};

struct ColumnParseCtx
{
public:
  ColumnParseCtx();
  ~ColumnParseCtx();

  void reset();
  void setup_buf(char *buf, int64_t buf_size, int64_t pos);
  bool is_valid() const;
  bool end() const;

  DECLARE_TO_STRING()
public:
  char *buf_;
  int64_t buf_size_;
  int64_t pos_;
  int64_t undecoded_col_cnt_;
  int64_t parsed_data_col_cnt_;
  RecordHeader *rec_hdr_;
  NullBitmap *null_bitmap_;
};

class ColumnFactory
{
public:
  static ColumnFactory &get_instance();

  int alloc_column(const schema::ColumnSchema &column_schema, Column *&column);
  int free_column(Column *&column);

private:
  ColumnFactory() {}
  ~ColumnFactory() {}
};

} // namespace table
} // namespace smartengine
