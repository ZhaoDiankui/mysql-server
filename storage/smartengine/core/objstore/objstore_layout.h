//  Copyright (c) 2023, ApeCloud Inc Holding Limited

#ifndef SMARTENGINE_INCLUDE_OBJSTORE_LAYOUT_H_
#define SMARTENGINE_INCLUDE_OBJSTORE_LAYOUT_H_

#include <string>

namespace smartengine {
namespace util {

std::string make_root_prefix(const std::string &cluster_id);

// Make object store lock prefix name
std::string make_lock_prefix(const std::string &cluster_id);

// Make index prefix name
std::string make_index_prefix(const std::string &cluster_id, const std::string &database_name, const int64_t index_id);

// Make extent prefix name
std::string make_data_prefix(const std::string &cluster_id, const std::string &database_name, const int64_t index_id);

std::string get_lease_lock_key(const std::string_view cluster_id);

std::string get_lease_lock_version_file_prefix(const std::string_view cluster_id);

} // namespace util
} // namespace smartengine

#endif