#include "FileStorage.h"

#include "coro/BlockOn.h"
#include "coro/WhenAll.h"

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace imager {

FileStorage::FileStorage(std::vector<std::filesystem::path> roots,
                         coro::ThreadPool& pool)
    : m_roots(std::move(roots)), m_pool(pool) {}

std::filesystem::path FileStorage::filePath(const std::filesystem::path& root,
                                             const std::string& id,
                                             const std::string& ext) const {
    std::string extNoDot = (ext.size() > 1 && ext[0] == '.') ? ext.substr(1) : ext;
    return root / id.substr(0, 2) / (id + "." + extNoDot);
}

// ---------------------------------------------------------------------------
// writeToRoot — single-root write coroutine, runs on a pool thread
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::writeToRoot(std::filesystem::path root,
                                           std::string id, std::string ext,
                                           Blob blob) {
    co_await m_pool.schedule();
    std::filesystem::path path = filePath(root, id, ext);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Cannot open for writing: " + path.string());
    out.write(reinterpret_cast<const char*>(blob.data()),
              static_cast<std::streamsize>(blob.size()));
    if (!out)
        throw std::runtime_error("Write failed: " + path.string());
}

// ---------------------------------------------------------------------------
// writeFileAsync — parallel write to all roots, with rollback on failure
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::writeFileAsync(const std::string& id,
                                              const std::string& ext,
                                              Blob blob) {
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
            if (!firstError) firstError = results[i];
        } else {
            succeededIdx.push_back(i);
        }
    }

    if (!firstError) co_return;

    // Roll back: delete from succeeded roots in parallel (best-effort)
    if (!succeededIdx.empty()) {
        std::vector<coro::Task<void>> cleanups;
        cleanups.reserve(succeededIdx.size());
        for (size_t idx : succeededIdx) {
            cleanups.push_back(
                [](coro::ThreadPool& pool,
                   std::filesystem::path path) -> coro::Task<void> {
                    co_await pool.schedule();
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                }(m_pool, filePath(m_roots[idx], id, ext))
            );
        }
        co_await coro::whenAll(std::move(cleanups));
    }

    std::rethrow_exception(firstError);
}

// ---------------------------------------------------------------------------
// deleteFileAsync — parallel delete from all roots (best-effort)
// ---------------------------------------------------------------------------

coro::Task<void> FileStorage::deleteFileAsync(const std::string& id,
                                               const std::string& ext) {
    std::vector<coro::Task<void>> tasks;
    tasks.reserve(m_roots.size());
    for (const auto& root : m_roots) {
        tasks.push_back(
            [](coro::ThreadPool& pool,
               std::filesystem::path path) -> coro::Task<void> {
                co_await pool.schedule();
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }(m_pool, filePath(root, id, ext))
        );
    }
    co_await coro::whenAll(std::move(tasks));
}

// ---------------------------------------------------------------------------
// Synchronous wrappers — delegate to async via blockOn
// ---------------------------------------------------------------------------

void FileStorage::writeFile(const std::string& id, const std::string& ext,
                             const Blob& blob) {
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
        if (!std::filesystem::exists(path)) continue;

        std::ifstream in(path, std::ios::binary);
        if (!in) continue;

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
        Blob blob(fileSize);
        in.read(reinterpret_cast<char*>(blob.writableData()),
                static_cast<std::streamsize>(fileSize));
        if (!in) continue; // read error — try next root

        blob.freeze();
        return blob;
    }
    return {}; // not found
}

} // namespace imager
