#pragma once

#include <cstdint>
#include <string>

namespace auth {

struct User {
  std::string login;
  std::string fullName;
  bool isAdmin{false};
  bool enabled{true};
  uint64_t createdAt{0}; // unix epoch seconds
  uint64_t updatedAt{0};
};

} // namespace auth
