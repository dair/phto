#pragma once

#include <cstdint>
#include <string>

namespace db {

struct File {
  std::string id;
  std::string name;
  uint64_t size{0}; ///< File size in bytes; values up to 2^63-1 are exact
  std::string ext;
};

} // namespace db
