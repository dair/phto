#include "FileStorage.h"

#include <coro/BlockOn.h>
#include <coro/WhenAll.h>
#include <metrics/Metrics.h>

#include <fstream>
#include <stdexcept>
#include <system_error>

#include "FileTimestamp.h"

namespace imager {

FileStorage::FileStorage(std::vector<std::filesystem::path> roots, coro::ThreadPool& pool, metrics::Metrics& metrics)
  : m_roots(std::move(roots)),
    m_pool(pool),
    m_metrics(metrics) {}

std::filesystem::path FileStorage::filePath(
  const std::filesystem::path& root, const std::string& id, const std::string& ext
) const {
  std::string extNoDot = (ext.size() > 1 && ext[0] == '.') ? ext.substr(1) : ext;
  return root / id.substr(0, 2) / (id + "." + extNoDot);
}

// ---------------------------------------------------------------------------
// writeToRoot — single-root write coroutine, runs on a pool thread
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::writeToRoot(std::filesystem::path root, std::string id, std::string ext, Blob blob) {
  co_await m_pool.schedule();
  metrics::Timer t(m_metrics.storage_write_root);
  std::filesystem::path path = filePath(root, id, ext);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Cannot open for writing: " + path.string());
  }
  out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
  if (!out) {
    throw std::runtime_error("Write failed: " + path.string());
  }
  m_metrics.storage_bytes_written.add(blob.size());
}

// ---------------------------------------------------------------------------
// writeFileAsync — parallel write to all roots, with rollback on failure
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::writeFileAsync(const std::string& id, const std::string& ext, Blob blob) {
  std::vector<coro::Task<void>> tasks;
  tasks.reserve(m_roots.size());
  for (const auto& root : m_roots) {
    // blob copied per task — each holds a shared_ptr reference
    tasks.push_back(writeToRoot(root, id, ext, blob));
  }

  // Run all and collect per-task outcomes without rethrowing
  auto results = co_await coro::whenAllSettled(std::move(tasks));

  // Find first error and which roots succeeded
  std::exception_ptr firstError;
  std::vector<size_t> succeededIdx;
  for (size_t i = 0; i < results.size(); ++i) {
    if (results[i]) {
      if (!firstError) {
        firstError = results[i];
      }
    } else {
      succeededIdx.push_back(i);
    }
  }

  if (!firstError) {
    co_return;
  }

  // Roll back: delete from succeeded roots in parallel (best-effort)
  if (!succeededIdx.empty()) {
    std::vector<coro::Task<void>> cleanups;
    cleanups.reserve(succeededIdx.size());
    for (size_t idx : succeededIdx) {
      cleanups.push_back([](coro::ThreadPool& pool, std::filesystem::path path) -> coro::Task<void> {
        co_await pool.schedule();
        std::error_code ec;
        std::filesystem::remove(path, ec);
      }(m_pool, filePath(m_roots[idx], id, ext)));
    }
    co_await coro::whenAll(std::move(cleanups));
  }

  std::rethrow_exception(firstError);
}

// ---------------------------------------------------------------------------
// deleteFileAsync — parallel delete from all roots (best-effort)
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::deleteFileAsync(const std::string& id, const std::string& ext) {
  std::vector<coro::Task<void>> tasks;
  tasks.reserve(m_roots.size());
  for (const auto& root : m_roots) {
    tasks.push_back([](coro::ThreadPool& pool, std::filesystem::path path) -> coro::Task<void> {
      co_await pool.schedule();
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }(m_pool, filePath(root, id, ext)));
  }
  co_await coro::whenAll(std::move(tasks));
}

// ---------------------------------------------------------------------------
// relocateFileAsync — atomic rename (or copy+delete) across all roots
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::relocateFileAsync(
  const std::string& oldId, const std::string& newId, const std::string& ext
) {
  std::vector<coro::Task<void>> tasks;
  tasks.reserve(m_roots.size());
  for (const auto& root : m_roots) {
    tasks.push_back(
      [](coro::ThreadPool& pool, std::filesystem::path oldPath, std::filesystem::path newPath) -> coro::Task<void> {
        co_await pool.schedule();
        if (!std::filesystem::exists(oldPath)) {
          co_return; // nothing to relocate on this root
        }
        // Ensure destination directory exists
        std::filesystem::create_directories(newPath.parent_path());
        std::error_code ec;
        std::filesystem::rename(oldPath, newPath, ec);
        if (ec) {
          // Cross-device or other rename failure: fallback to copy + delete
          std::filesystem::copy_file(oldPath, newPath, std::filesystem::copy_options::overwrite_existing, ec);
          if (ec) {
            throw std::runtime_error("relocate copy failed: " + ec.message());
          }
          // Best-effort: preserve timestamps from old path to new path after cross-device copy.
          // oldPath still exists here (removed below), so stat is safe.
          try {
            copyTimestamps(oldPath, newPath);
          } catch (...) {
            // Non-fatal — relocation must not fail just because timestamps could not be set.
          }
          std::filesystem::remove(oldPath, ec);
        }
      }(m_pool, filePath(root, oldId, ext), filePath(root, newId, ext))
    );
  }
  co_await coro::whenAll(std::move(tasks));
}

// ---------------------------------------------------------------------------
// writeToRootFromDisk — single-root streaming copy coroutine
// ---------------------------------------------------------------------------

static constexpr size_t STREAM_CHUNK_SIZE = 4ULL * 1024ULL * 1024ULL; // 4 MB

coro::Task<void> FileStorage::writeToRootFromDisk(
  std::filesystem::path root, std::string id, std::string ext, std::filesystem::path sourcePath
) {
  co_await m_pool.schedule();
  metrics::Timer t(m_metrics.storage_write_root);

  auto destPath = filePath(root, id, ext);
  std::filesystem::create_directories(destPath.parent_path());

  std::ifstream in(sourcePath, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Cannot open source: " + sourcePath.string());
  }

  std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Cannot open dest: " + destPath.string());
  }

  std::vector<char> buf(STREAM_CHUNK_SIZE);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    auto n = in.gcount();
    if (n > 0) {
      out.write(buf.data(), n);
      if (!out) {
        throw std::runtime_error("Write failed: " + destPath.string());
      }
      m_metrics.storage_bytes_written.add(static_cast<uint64_t>(n));
    }
  }
  if (in.bad()) {
    throw std::runtime_error("Read error: " + sourcePath.string());
  }
}

// ---------------------------------------------------------------------------
// writeFileFromDiskAsync — parallel stream-copy to all roots, with rollback
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::writeFileFromDiskAsync(
  const std::string& id, const std::string& ext, const std::filesystem::path& sourcePath
) {
  std::vector<coro::Task<void>> tasks;
  tasks.reserve(m_roots.size());
  for (const auto& root : m_roots) {
    tasks.push_back(writeToRootFromDisk(root, id, ext, sourcePath));
  }

  auto results = co_await coro::whenAllSettled(std::move(tasks));

  std::exception_ptr firstError;
  std::vector<size_t> succeededIdx;
  for (size_t i = 0; i < results.size(); ++i) {
    if (results[i]) {
      if (!firstError) {
        firstError = results[i];
      }
    } else {
      succeededIdx.push_back(i);
    }
  }

  if (!firstError) {
    co_return;
  }

  // Roll back: delete from succeeded roots in parallel (best-effort).
  if (!succeededIdx.empty()) {
    std::vector<coro::Task<void>> cleanups;
    cleanups.reserve(succeededIdx.size());
    for (size_t idx : succeededIdx) {
      cleanups.push_back([](coro::ThreadPool& pool, std::filesystem::path path) -> coro::Task<void> {
        co_await pool.schedule();
        std::error_code ec;
        std::filesystem::remove(path, ec);
      }(m_pool, filePath(m_roots[idx], id, ext)));
    }
    co_await coro::whenAll(std::move(cleanups));
  }

  std::rethrow_exception(firstError);
}

// ---------------------------------------------------------------------------
// applyTimestamps / applyTimestampsFromSource — set mtime+atime on all copies
// ---------------------------------------------------------------------------

void FileStorage::applyTimestamps(const std::string& id, const std::string& ext, const struct timespec times[2]) {
  for (const auto& root : m_roots) {
    std::filesystem::path destPath = filePath(root, id, ext);
    try {
      imager::applyTimestamps(destPath, times);
    } catch (...) {
      // Best-effort: skip roots where timestamps cannot be applied.
    }
  }
}

void FileStorage::applyTimestampsFromSource(
  const std::string& id, const std::string& ext, const std::filesystem::path& sourcePath
) {
  for (const auto& root : m_roots) {
    std::filesystem::path destPath = filePath(root, id, ext);
    try {
      copyTimestamps(sourcePath, destPath);
    } catch (...) {
      // Best-effort: skip roots where timestamps cannot be applied.
    }
  }
}

// ---------------------------------------------------------------------------
// Synchronous wrappers — delegate to async via blockOn
// ---------------------------------------------------------------------------

void FileStorage::writeFile(const std::string& id, const std::string& ext, const Blob& blob) {
  coro::blockOn(m_pool, writeFileAsync(id, ext, blob));
}

void FileStorage::deleteFile(const std::string& id, const std::string& ext) {
  coro::blockOn(m_pool, deleteFileAsync(id, ext));
}

// ---------------------------------------------------------------------------
// readFile — sequential, reads from first available root
// ---------------------------------------------------------------------------

Blob FileStorage::readFile(const std::string& id, const std::string& ext) {
  for (const auto& root : m_roots) {
    std::filesystem::path path = filePath(root, id, ext);
    if (!std::filesystem::exists(path)) {
      continue;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
      continue;
    }

    // Get file size
    in.seekg(0, std::ios::end);
    const auto fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (fileSize == 0) {
      Blob empty(0u);
      empty.freeze();
      return empty;
    }

    // Allocate, fill directly, freeze — zero extra copy
    metrics::Timer t(m_metrics.storage_read_duration);
    Blob blob(fileSize);
    in.read(reinterpret_cast<char*>(blob.writableData()), static_cast<std::streamsize>(fileSize));
    if (!in) {
      continue; // read error — try next root
    }

    blob.freeze();
    m_metrics.storage_bytes_read.add(fileSize);
    return blob;
  }
  return {}; // not found
}

} // namespace imager
