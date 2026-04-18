#pragma once

#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <filesystem>
#include <system_error>

namespace imager {

/// Read the mtime and atime from sourcePath and return them as a two-element
/// array: times[0] = atime, times[1] = mtime.
/// Throws std::system_error on stat failure.
/// Call this BEFORE opening/reading the source file to capture the original
/// atime before any read operation updates it.
void readTimestamps(const std::filesystem::path& sourcePath, struct timespec times[2]);

/// Apply pre-read timestamps to destPath using utimensat(2).
/// times[0] = atime, times[1] = mtime (same layout as utimensat).
/// Throws std::system_error on utimensat failure.
void applyTimestamps(const std::filesystem::path& destPath, const struct timespec times[2]);

/// Convenience wrapper: read timestamps from sourcePath and immediately apply
/// them to destPath. Equivalent to readTimestamps + applyTimestamps.
/// Throws std::system_error on stat or utimensat failure.
/// Note: if the caller has already opened sourcePath for reading, the atime of
/// sourcePath may already be updated to "now" — use readTimestamps +
/// applyTimestamps separately to capture atime before reading.
void copyTimestamps(const std::filesystem::path& sourcePath, const std::filesystem::path& destPath);

} // namespace imager
