#pragma once

#include <coro/Task.h>
#include <coro/ThreadPool.h>
#include <imager/types/Blob.h>
#include <metrics/Metrics.h>
#include <sys/stat.h>

#include <filesystem>
#include <string>
#include <vector>

namespace imager {

/// Manages file I/O across multiple redundant storage roots.
/// File layout within each root: <root>/<first-2-hex-chars>/<sha256>.<ext-no-dot>
///
/// Async methods (writeFileAsync, deleteFileAsync) dispatch work to the
/// provided thread pool and run all roots in parallel.
/// Synchronous methods (writeFile, deleteFile, readFile) delegate to the
/// async versions via blockOn and are kept for call-sites that don't co_await.
class FileStorage {
public:
  explicit FileStorage(std::vector<std::filesystem::path> roots, coro::ThreadPool& pool, metrics::Metrics& metrics);

  FileStorage(const FileStorage&) = delete;
  FileStorage& operator=(const FileStorage&) = delete;
  FileStorage(FileStorage&&) = delete;
  FileStorage& operator=(FileStorage&&) = delete;

  // --- Synchronous API (blocks until complete) ---

  /// Write blob to ALL roots. Rolls back on partial failure.
  void writeFile(const std::string& id, const std::string& ext, const Blob& blob);

  /// Read from first available root. Returns empty Blob if not found.
  Blob readFile(const std::string& id, const std::string& ext);

  /// Delete from all roots (best-effort, errors ignored).
  void deleteFile(const std::string& id, const std::string& ext);

  /// Apply pre-read timestamps to the stored file on every root.
  /// times[0] = atime, times[1] = mtime (same layout as utimensat).
  /// Errors on individual roots are silently skipped (best-effort).
  /// Prefer this overload when the caller has already read the source file
  /// (which would update its atime); read timestamps BEFORE opening the source.
  void applyTimestamps(const std::string& id, const std::string& ext, const struct timespec times[2]);

  /// Copy mtime and atime from sourcePath to the stored file on every root.
  /// Called after a successful write so that stored copies carry the original
  /// file's timestamps rather than the write time.
  /// Errors on individual roots are silently skipped (best-effort).
  void applyTimestampsFromSource(
    const std::string& id, const std::string& ext, const std::filesystem::path& sourcePath
  );

  // --- Async coroutine API ---

  /// Write blob to ALL roots in parallel. Rolls back on partial failure.
  /// Takes Blob by value so each per-root coroutine shares ownership.
  coro::Task<void> writeFileAsync(const std::string& id, const std::string& ext, Blob blob);

  /// Delete from all roots in parallel (best-effort).
  coro::Task<void> deleteFileAsync(const std::string& id, const std::string& ext);

  /// Move a file from one id-based path to another across all storage roots.
  /// Used when an orphan sidecar's parent is discovered (Scenario B resolution).
  /// Uses std::filesystem::rename (atomic on same filesystem); falls back to
  /// copy+delete if rename fails (e.g., cross-device).
  coro::Task<void> relocateFileAsync(const std::string& oldId, const std::string& newId, const std::string& ext);

  /// Stream-copy from a source file on disk to ALL storage roots in parallel.
  /// Reads in 4 MB chunks — never holds the full file in memory.
  /// Rolls back on partial failure (same semantics as writeFileAsync).
  coro::Task<void> writeFileFromDiskAsync(
    const std::string& id, const std::string& ext, const std::filesystem::path& sourcePath
  );

private:
  std::vector<std::filesystem::path> m_roots;
  coro::ThreadPool& m_pool;
  metrics::Metrics& m_metrics;

  std::filesystem::path filePath(
    const std::filesystem::path& root, const std::string& id, const std::string& ext
  ) const;

  /// Write blob to a single root on a pool thread.
  coro::Task<void> writeToRoot(std::filesystem::path root, std::string id, std::string ext, Blob blob);

  /// Stream-copy from source file to one storage root on a pool thread.
  coro::Task<void> writeToRootFromDisk(
    std::filesystem::path root, std::string id, std::string ext, std::filesystem::path sourcePath
  );
};

} // namespace imager
