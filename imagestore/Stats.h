#pragma once

#include <atomic>
#include <cstdint>

namespace imagestore {

struct Stats {
  std::atomic<uint64_t> processed{0};
  std::atomic<uint64_t> added{0};       ///< Import mode: files added. Delete mode: files deleted.
  std::atomic<uint64_t> duplicates{0};  ///< Import mode: duplicate files. Delete mode: missing (not an error).
  std::atomic<uint64_t> errors{0};
  std::atomic<uint64_t> skipped{0};
  std::atomic<uint64_t> totalBytes{0};
};

} // namespace imagestore
