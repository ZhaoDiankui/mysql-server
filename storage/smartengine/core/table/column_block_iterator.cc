#include "table/column_block_iterator.h"
#include "table/block_struct.h"

namespace smartengine
{
using namespace common;
using namespace memory;

namespace table
{

ColumnBlockIterator::ColumnBlockIterator()
    : is_inited_(false),
      block_data_(nullptr),
      block_size_(0),
      row_count_(0),
      row_cursor_(0),
      pos_(0),
      table_schema_(),
      parse_ctx_(),
      readers_(),
      key_(ModId::kColumnBlockReader),
      value_(ModId::kColumnBlockReader)
{}

ColumnBlockIterator::~ColumnBlockIterator()
{
  destroy();
}

int ColumnBlockIterator::init(const Slice &block, const BlockInfo &block_info, const TableSchema &table_schema)
{
  int ret = Status::kOk;

  if (UNLIKELY(is_inited_)) {
    ret = Status::kInitTwice;
    SE_LOG(WARN, "ColumnBlockIterator has been inited", K(ret));
  } else if (IS_NULL(block.data()) || UNLIKELY(0 == block.size())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "Invalid argument", K(ret), KP(block.data()), K(block.size()));
  } else if (FAILED(init_column_unit_readers(block, block_info.get_unit_infos(), table_schema.get_column_schemas(), block_info.get_row_count()))) {
    SE_LOG(WARN, "fail to init column unit readers", K(ret), K(block_info));
  } else {
    block_data_ = block.data();
    block_size_ = block.size();
    row_count_ = block_info.get_row_count();
    row_cursor_ = 0;
    table_schema_ = table_schema;
    is_inited_ = true;
  }

  return ret;
}

void ColumnBlockIterator::destroy()
{
  for (uint32_t i = 0; i < readers_.size(); ++i) {
    MOD_DELETE_OBJECT(ColumnUnitReader, readers_[i]);
  }
  readers_.clear();
}

int ColumnBlockIterator::get_next_row(Slice &key, Slice &value)
{
  int ret = Status::kOk;
  ColumnArray columns;

  if (UNLIKELY(!is_inited_)) {
    ret = Status::kNotInit;
    SE_LOG(WARN, "SeColumnBlockIterator should been inited first", K(ret));
  } else if (FAILED(get_next_columns(columns))) {
    if (Status::kIterEnd != ret) {
      SE_LOG(WARN, "Failed to get next columns", K(ret));
    } else {
      se_assert(row_count_ == row_cursor_);
    }
  } else if (FAILED(transform_to_row(columns))) {
    SE_LOG(WARN, "Failed to transform to kv format", K(ret));
  } else {
    key.assign(key_.data(), key_.size());
    value.assign(value_.data(), value_.size());
    ++row_cursor_;
  }

  return ret;
}

int ColumnBlockIterator::init_column_unit_readers(const Slice &block_data,
                                                  const std::vector<ColumnUnitInfo> &unit_infos,
                                                  const ColumnSchemaArray &column_schemas,
                                                  int64_t column_count)
{
  int ret = Status::kOk;
  ColumnUnitReader *unit_reader = nullptr;

  if (UNLIKELY(unit_infos.size() > column_schemas.size())) {
    ret = Status::kInvalidArgument;
    SE_LOG(WARN, "invalid argument", K(ret), "unit_info_size", unit_infos.size(), "column_schema_size", column_schemas.size());
  } else {
    for (uint32_t i = 0; SUCCED(ret) && i < unit_infos.size(); ++i) {
      const ColumnSchema &column_schema = column_schemas[i];
      const ColumnUnitInfo &unit_info = unit_infos[i];
      Slice unit_data(block_data.data() + unit_info.data_offset_, unit_info.data_size_);
      unit_reader = nullptr;
      if (IS_NULL(unit_reader = MOD_NEW_OBJECT(ModId::kColumnBlockReader, ColumnUnitReader))) {
        ret = Status::kMemoryLimit;
        SE_LOG(WARN, "fail to allocate memory for ColumnUnitReader", K(ret));
      } else if (FAILED(unit_reader->init(unit_data, unit_info, column_schema, column_count))) {
        SE_LOG(WARN, "fail to init ColumnUnitReader", K(ret), K(unit_info), K(column_schema));
      } else {
        readers_.push_back(unit_reader);
      }
    }
  }

  // Resource clean
  if (FAILED(ret)) {
    if (IS_NOTNULL(unit_reader)) {
      MOD_DELETE_OBJECT(ColumnUnitReader, unit_reader);
    }
    for (uint32_t i = 0; i < readers_.size(); ++i) {
      MOD_DELETE_OBJECT(ColumnUnitReader, readers_[i]);
    }
    readers_.clear();
  }

  return ret;
}

int ColumnBlockIterator::get_next_columns(ColumnArray &columns)
{
  int ret = Status::kOk;
  Column *column = nullptr;
  columns.clear();
  parse_ctx_.reset();
  parse_ctx_.undecoded_col_cnt_ = table_schema_.get_packed_column_count();

  for (uint32_t i = 0; SUCCED(ret) && i < readers_.size(); ++i) {
    if (FAILED(readers_[i]->get_next_column(parse_ctx_, column))) {
      if (Status::kIterEnd != ret) {
        SE_LOG(WARN, "Failed to get next column", K(ret), K(i));
      } else {
        se_assert(0 == i && readers_[i]->reach_end());
      }
    } else {
      columns.push_back(column);
      if (i > 0 && parse_ctx_.end()) {
        break;
      }
    }
  }

  return ret;
}

int ColumnBlockIterator::transform_to_row(const ColumnArray &columns)
{
  int ret = Status::kOk;
  Column *column = nullptr;
  key_.reuse();
  value_.reuse();

  for (uint32_t i = 0; SUCCED(ret) && i < columns.size(); ++i) {
    if (IS_NULL(column = columns[i])) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "The column is nullptr", K(ret), K(i), KP(column));
    } else if (UNLIKELY(!column->is_valid())) {
      ret = Status::kErrorUnexpected;
      SE_LOG(WARN, "The column is invalid", K(ret), K(i));
    } else {
      if (0 == i) {
        //Primary key
        if (!column->is_primary()) {
          ret = Status::kCorruption;
          SE_LOG(WARN, "The first column must be primary key", K(ret), K(i));
        } else if (FAILED(key_.write(column->buf(), column->size()))) {
          SE_LOG(WARN, "Failed to write primary key", K(ret));
        } else {
          //succeed
        }
      } else {
        //Non-Primary columns
        if (FAILED(value_.write(column->buf(), column->size()))) {
          SE_LOG(WARN, "Failed to write non-primary key", K(ret));
        }
      }
    }
  }

  return ret; 
}

} // namespace table
} // namespace smartengine