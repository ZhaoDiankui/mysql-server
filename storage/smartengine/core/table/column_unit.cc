#include "table/column_unit.h"
#include "logger/log_module.h"

namespace smartengine
{
using namespace common;
using namespace memory;
using namespace schema;
using namespace util;

namespace table
{

ColumnUnitInfo::ColumnUnitInfo()
    : column_type_(INVALID_COLUMN_TYPE),
      compress_type_(kNoCompression),
      column_count_(0),
      null_column_count_(0),
      data_offset_(0),
      data_size_(0),
      raw_data_size_(0)
{}

ColumnUnitInfo::ColumnUnitInfo(const ColumnUnitInfo &column_unit_info)
    : column_type_(column_unit_info.column_type_),
      compress_type_(column_unit_info.compress_type_),
      column_count_(column_unit_info.column_count_),
      null_column_count_(column_unit_info.null_column_count_),
      data_offset_(column_unit_info.data_offset_),
      data_size_(column_unit_info.data_size_),
      raw_data_size_(column_unit_info.raw_data_size_)
{}

ColumnUnitInfo &ColumnUnitInfo::operator=(const ColumnUnitInfo &column_unit_info)
{
  column_type_ = column_unit_info.column_type_;
  compress_type_ = column_unit_info.compress_type_;
  column_count_ = column_unit_info.column_count_;
  null_column_count_ = column_unit_info.null_column_count_;
  data_offset_ = column_unit_info.data_offset_;
  data_size_ = column_unit_info.data_size_;
  raw_data_size_ = column_unit_info.raw_data_size_;

  return *this;
}

ColumnUnitInfo::~ColumnUnitInfo() {}

void ColumnUnitInfo::reset()
{
  column_type_ = INVALID_COLUMN_TYPE;
  compress_type_ = kNoCompression;
  column_count_ = 0;
  null_column_count_ = 0;
  data_offset_ = 0;
  data_size_ = 0;
  raw_data_size_ = 0;
}

bool ColumnUnitInfo::is_valid() const
{
  return column_type_ > INVALID_COLUMN_TYPE &&
         column_count_ > 0 && null_column_count_ >= 0 &&
         data_offset_ >= 0 && data_size_ >= 0
         && raw_data_size_ >= 0;
}


DEFINE_TO_STRING(ColumnUnitInfo, KV_(column_type), KV_(compress_type), KV_(column_count),
    KV_(null_column_count), KV_(data_offset), KV_(data_size), KV_(raw_data_size))

DEFINE_COMPACTIPLE_SERIALIZATION(ColumnUnitInfo, column_type_, compress_type_, column_count_,
    null_column_count_, data_offset_, data_size_, raw_data_size_)

ColumnUnitWriter::ColumnUnitWriter()
    : is_inited_(false),
      column_schema_(),
      compress_type_(kNoCompression),
      compressor_helper_(),
      column_count_(0),
      null_column_count_(0),
      buf_(ModId::kColumnUnitWriter)
{}

ColumnUnitWriter::~ColumnUnitWriter() {}

int ColumnUnitWriter::init(const ColumnSchema &column_schema, const CompressionType compress_type)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "SeColumnUnitWriter isn't inited", K(ret));
  } else if (UNLIKELY(!column_schema.is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "Invalid argument", K(ret), K(column_schema));
  } else if (FAILED(buf_.reserve(DEFAULT_COLUMN_UNIT_BUFFER_SIZE))) {
    SE_LOG(WARN, "Fail to reserve buffer", K(ret));
  } else {
    column_schema_ = column_schema;
    compress_type_ = compress_type;
    is_inited_ = true;
  }

  return ret;
}

void ColumnUnitWriter::reuse()
{
  column_count_ = 0;
  null_column_count_ = 0;
  buf_.reuse();
}

int ColumnUnitWriter::append(const Column *column)
{
  int ret = Status::kOk;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "SeColumnUnitWriter should been inited first", K(ret));
  } else if (IS_NULL(column)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "Invalid argument", K(ret), KP(column));
  } else if (UNLIKELY(!column->is_valid())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "Invalid argument", K(ret), K(*column));
  } else if (column_schema_.is_primary()) {
    if (FAILED(write_primary_column(column))) {
      SE_LOG(WARN, "failed to write primary column", K(ret));
    }
  } else {
    if (FAILED(write_normal_column(column))) {
      SE_LOG(WARN, "failed to write normal column", K(ret)) ;
    }
  }

  return ret;
}

int ColumnUnitWriter::build(Slice &unit_data, ColumnUnitInfo &unit_info)
{
  int ret = Status::kOk;
  CompressionType actual_compress_type = common::kNoCompression;
  Slice raw_unit(buf_.data(), buf_.size());

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "SeColumnUnitWriter isn't inited", K(ret));
  } else {
    unit_info.column_type_ = column_schema_.get_type();
    unit_info.column_count_ = column_count_;
    unit_info.null_column_count_ = null_column_count_;
    unit_info.raw_data_size_ = raw_unit.size();

    if (raw_unit.empty()) {
      unit_info.compress_type_ = common::kNoCompression;
      unit_info.data_size_ = 0;
    } else if (FAILED(compressor_helper_.compress(raw_unit, compress_type_, unit_data, actual_compress_type))) {
      SE_LOG(WARN, "fail to compress unit data", K(ret), K(raw_unit), KE_(compress_type), KE(actual_compress_type));
    } else {
      unit_info.compress_type_ = actual_compress_type;
      unit_info.data_size_ = unit_data.size();
    }

#ifndef NDEBUG
    SE_LOG(INFO, "success to build unit", K(unit_info));
#endif
  }

  return ret;
}

int64_t ColumnUnitWriter::current_size() const
{
  return buf_.size();
}

int ColumnUnitWriter::write_primary_column(const Column *column)
{
  assert(column);
  int ret = Status::kOk;
  int32_t size = column->size();

  if (UNLIKELY(column->is_null())) {
    ret = Status::kErrorUnexpected;
    SE_LOG(WARN, "the primary column must not been nullptr", K(ret), K(*column));
  //TODO(Zhao Dongsheng) : use varibale encoding?
  } else if (FAILED(buf_.write_pod(size))) {
    SE_LOG(WARN, "failed to write primary column size", K(ret));
  } else if (FAILED(buf_.write(column->buf(), column->size()))) {
    SE_LOG(WARN, "failed to write primary column data", K(ret));
  } else {
    ++column_count_;

#ifndef NDEBUG
    SE_LOG(INFO, "success to write primary column", K(size), "column_size", column->size());
#endif
  }

  return ret;
}

int ColumnUnitWriter::write_normal_column(const Column *column)
{
  assert(column);
  int ret = Status::kOk;

  if (column->is_null()) {
    ++null_column_count_;
    ++column_count_;
  // The column may be empty, like type char(0).
  } else if (FAILED(buf_.write(column->buf(), column->size()))) {
    SE_LOG(WARN, "failed to write normal column data", K(ret));
  } else {
    ++column_count_;

#ifndef NDEBUG
    SE_LOG(INFO, "success to write normal column", "type", column->type(), "column_size", column->size());
#endif
  }

  return ret;
}

ColumnUnitReader::ColumnUnitReader()
    : is_inited_(false),
      raw_unit_buf_(nullptr),
      raw_unit_data_(),
      column_count_(0),
      column_cursor_(0),
      pos_(0),
      column_schema_(),
      column_(nullptr)
{}

ColumnUnitReader::~ColumnUnitReader()
{
  destroy();
}

int ColumnUnitReader::init(const Slice &unit_data, const ColumnUnitInfo &unit_info, const ColumnSchema &column_schema, int64_t column_count)
{
  int ret = Status::kOk;
  CompressorHelper compressor_helper;
  assert(unit_info.column_type_ == column_schema.get_type());

  if (UNLIKELY(is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "ColumnUnitReader should be inited", K(ret));
  } else if (UNLIKELY(!unit_info.is_valid()) || UNLIKELY(!column_schema.is_valid()) || UNLIKELY(column_count <= 0)) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), K(unit_info), K(column_schema), K(column_count));
  } else if (FAILED(ColumnFactory::get_instance().alloc_column(column_schema, column_))) {
    SE_LOG(WARN, "fail to allocate column", K(ret), K(column_schema), K(unit_info));
  } else {
    if (kNoCompression == unit_info.compress_type_) {
      raw_unit_data_.assign(unit_data.data(), unit_data.size());
    } else {
      if (IS_NULL(raw_unit_buf_ = reinterpret_cast<char *>(base_malloc(unit_info.raw_data_size_, ModId::kColumnUnitReader)))) {
        ret = Status::kMemoryLimit;
        SE_LOG(WARN, "fail to allocate memory for unit data", K(ret), K(unit_info));
      } else if (FAILED(compressor_helper.uncompress(unit_data,
                                                     static_cast<CompressionType>(unit_info.compress_type_),
                                                     raw_unit_buf_,
                                                     unit_info.raw_data_size_,
                                                     raw_unit_data_))) {
        SE_LOG(WARN, "fail to uncompress unit data", K(ret));
      } 
    }

    if (SUCCED(ret)) {
      column_count_ = column_count;
      column_cursor_ = 0;
      pos_ = 0;
      column_schema_ = column_schema;
      is_inited_ = true;
    }
  }

  if (FAILED(ret)) {
    destroy();
  }

  return ret;
}

void ColumnUnitReader::destroy()
{
  if (IS_NOTNULL(column_)) {
    ColumnFactory::get_instance().free_column(column_) ;   
    column_ = nullptr;
  }

  if (IS_NOTNULL(raw_unit_buf_)) {
    base_free(raw_unit_buf_);
    raw_unit_buf_ = nullptr;
  }
}

int ColumnUnitReader::get_next_column(ColumnParseCtx &parse_ctx, Column *&column)
{
  int ret = Status::kOk;
  column_->reuse();
  //TODO: Zhao Dongsheng, should not const_cast?
  parse_ctx.setup_buf(const_cast<char *>(raw_unit_data_.data()), raw_unit_data_.size(), pos_);

  if (!is_inited_) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "SeColumnUnitReader should been inited first", K(ret));
  } else if (!parse_ctx.is_valid()) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "Invalid argument", K(ret), K(parse_ctx));
  } else if (reach_end()) {
    se_assert(pos_ == static_cast<int64_t>(raw_unit_data_.size()));
    ret = Status::kIterEnd;
  } else {
    if (FAILED(column_->parse(parse_ctx))) {
      SE_LOG(WARN, "Failed to parse column", K(ret));
    } else {
      /**Update position.*/
      pos_ = parse_ctx.pos_;
      column = column_;
      ++column_cursor_;

#ifndef NDEBUG
      SE_LOG(INFO, "success to parse column", "type", column_->type(), "size", column->size());
#endif
    }
  }

  return ret;
}

} // namespace table
} // namespace smartengine