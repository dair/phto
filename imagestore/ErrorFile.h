#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>

namespace imagestore {

/// Read/write error file used as both a skip-list and an error log.
///
/// On construction, existing lines are loaded into an in-memory set.
/// Any path in that set is skipped when encountered on stdin.
/// When a file fails processing, its path is appended to the file and
/// added to the in-memory set (deduplicated).
///
/// Thread-safe: recordError() is protected by a mutex.
class ErrorFile {
public:
  /// Open the error file at the given path.
  /// Reads existing lines into the skip set.
  /// Creates the file if it does not exist.
  /// Throws std::runtime_error if the file cannot be opened for appending.
  explicit ErrorFile(const std::filesystem::path& path);

  ErrorFile(const ErrorFile&) = delete;
  ErrorFile& operator=(const ErrorFile&) = delete;

  /// Returns true if absPath should be skipped (already in the skip set).
  bool shouldSkip(const std::string& absPath) const;

  /// Record a failed path. Thread-safe. Deduplicates against both the
  /// initial load and previous appends within this run.
  void recordError(const std::string& absPath);

  /// Number of paths skipped (present in the file on startup).
  size_t initialSkipCount() const {
    return m_initialCount;
  }

  /// Number of new errors recorded during this run.
  size_t errorCount() const;

private:
  std::unordered_set<std::string> m_known;
  std::ofstream m_out;
  mutable std::mutex m_mutex;
  size_t m_initialCount{0};
  size_t m_errorCount{0};
};

} // namespace imagestore
