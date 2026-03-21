#pragma once

#include "imager/types/Blob.h"
#include "coro/Task.h"
#include "coro/ThreadPool.h"

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
    explicit FileStorage(std::vector<std::filesystem::path> roots,
                         coro::ThreadPool& pool);

    // --- Synchronous API (blocks until complete) ---

    /// Write blob to ALL roots. Rolls back on partial failure.
    void writeFile(const std::string& id, const std::string& ext, const Blob& blob);

    /// Read from first available root. Returns empty Blob if not found.
    Blob readFile(const std::string& id, const std::string& ext);

    /// Delete from all roots (best-effort, errors ignored).
    void deleteFile(const std::string& id, const std::string& ext);

    // --- Async coroutine API ---

    /// Write blob to ALL roots in parallel. Rolls back on partial failure.
    /// Takes Blob by value so each per-root coroutine shares ownership.
    coro::Task<void> writeFileAsync(const std::string& id, const std::string& ext,
                                    Blob blob);

    /// Delete from all roots in parallel (best-effort).
    coro::Task<void> deleteFileAsync(const std::string& id, const std::string& ext);

private:
    std::vector<std::filesystem::path> m_roots;
    coro::ThreadPool&                  m_pool;

    std::filesystem::path filePath(const std::filesystem::path& root,
                                   const std::string& id,
                                   const std::string& ext) const;

    /// Write blob to a single root on a pool thread.
    coro::Task<void> writeToRoot(std::filesystem::path root,
                                 std::string id, std::string ext,
                                 Blob blob);
};

} // namespace imager
