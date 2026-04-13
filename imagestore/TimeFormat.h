#pragma once

#include <cstdint>
#include <iomanip>
#include <ostream>

namespace imagestore {

/// Format a non-negative number of seconds as HH:MM:SS into `out`.
/// Hours are unbounded (e.g. 7200s → "02h:00m:00s").
inline void fmtElapsed(std::ostream& out, int64_t totalSeconds) {
  int64_t h = totalSeconds / 3600;
  int64_t m = (totalSeconds % 3600) / 60;
  int64_t s = totalSeconds % 60;
  out << std::setfill('0') << std::setw(2) << h << "h:" << std::setw(2) << m << "m:" << std::setw(2) << s << "s"
      << std::setfill(' ');
}

} // namespace imagestore
