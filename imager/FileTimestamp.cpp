#include "FileTimestamp.h"

namespace imager {

void readTimestamps(const std::filesystem::path& sourcePath, struct timespec times[2]) {
  struct stat st{};
  if (::stat(sourcePath.c_str(), &st) != 0) {
    throw std::system_error(errno, std::system_category(), "stat failed: " + sourcePath.string());
  }
  // times[0] = atime, times[1] = mtime
#ifdef __linux__
  times[0] = st.st_atim;
  times[1] = st.st_mtim;
#else
  // macOS / BSD
  times[0] = st.st_atimespec;
  times[1] = st.st_mtimespec;
#endif
}

void applyTimestamps(const std::filesystem::path& destPath, const struct timespec times[2]) {
  if (::utimensat(AT_FDCWD, destPath.c_str(), times, 0) != 0) {
    throw std::system_error(errno, std::system_category(), "utimensat failed: " + destPath.string());
  }
}

void copyTimestamps(const std::filesystem::path& sourcePath, const std::filesystem::path& destPath) {
  struct timespec times[2];
  readTimestamps(sourcePath, times);
  applyTimestamps(destPath, times);
}

} // namespace imager
