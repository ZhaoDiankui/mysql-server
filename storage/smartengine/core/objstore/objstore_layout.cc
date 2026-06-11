//  Copyright (c) 2023, ApeCloud Inc Holding Limited

#include "objstore_layout.h"

namespace smartengine {

namespace util {

std::string make_root_prefix(const std::string &cluster_id)
{
  // clang-format off
  std::string prefix = cluster_id + 
                       "/" + "smartengine" +
                       "/" + "v1" +
                       "/";
  // clang-format on
  return prefix;
}

// Make object store lock prefix name
std::string make_lock_prefix(const std::string &cluster_id)
{
  std::string prefix = make_root_prefix(cluster_id) + "locks" + "/";
  return prefix;
}

std::string make_index_prefix(const std::string &cluster_id, const std::string &database_name, const int64_t index_id)
{
  // clang-format off
  std::string prefix = make_root_prefix(cluster_id) + database_name + 
                      "/" + std::to_string(index_id) + 
                      "/";
  // clang-format on
  return prefix;
}

std::string make_data_prefix(const std::string &cluster_id, const std::string &database_name, const int64_t index_id)
{
  std::string prefix = make_index_prefix(cluster_id, database_name, index_id) + "data" + "/";

  return prefix;
}

std::string get_lease_lock_key(const std::string_view cluster_id)
{
  return util::make_lock_prefix(cluster_id.data()) + "lease_lock";
}

std::string get_lease_lock_version_file_prefix(const std::string_view cluster_id)
{
  return util::make_lock_prefix(cluster_id.data()) + "lease_lock_version_";
}

} // namespace util
} // namespace smartengine