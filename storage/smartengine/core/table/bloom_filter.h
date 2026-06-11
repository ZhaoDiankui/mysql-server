#pragma once

#include <cstdint>
#include <vector>
namespace smartengine
{
namespace common
{
  class Slice;
}

namespace table
{
class BloomFilter
{
public:
  // maybe configurable in the future.
  static const int32_t DEFAULT_PER_KEY_BITS = 10;
  // maybe configurable in the future.
  static const int32_t DEFAULT_PROBE_NUM = 7;
};

// TODO (Zhao Dongsheng) : In scenarios storage space is sensitive but
// performance is not critical, it is neccessary to provide the ability
// to disable persistent bloom filter or build memory bloom filter dynamically
// according empty read count.
class BloomFilterWriter
{
public:
  BloomFilterWriter();
  ~BloomFilterWriter();

  int init(int32_t per_key_bits, int32_t probe_num);
  void destroy();
  void reuse();
  int add(const common::Slice &key);
  int build(common::Slice &bloom_filter);
  static void calculate_space_size(int32_t hash_value_size,
                                   int32_t per_key_bits,
                                   int32_t &size,
                                   int32_t &cache_line_num);

private:
  int reserve_bits_buf(int32_t size);
  void set_bits(uint32_t hash_value, int32_t cache_line_num);

private:
  bool is_inited_;
  int32_t per_key_bits_;
  int32_t probe_num_;
  std::vector<uint32_t> hash_values_;
  char *bits_buf_;
  int32_t bits_buf_size_;
};

class BloomFilterReader
{
public:
  BloomFilterReader();
  ~BloomFilterReader();

  int init(const common::Slice &bloom_filter, int32_t probe_num);
  int check(const common::Slice &key, bool &exist);

private:
  bool is_inited_;
  const char *bits_buf_;
  int32_t bits_buf_size_; 
  int32_t probe_num_;
  int32_t cache_line_num_;
};

} // namespace table
} // namespace smartengine