#pragma once

#include <cstdint>

namespace db {

struct Pagination {
  uint32_t offset{0};
  uint32_t limit{50};
};

} // namespace db
