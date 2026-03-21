#include "MultiDatabase.h"

#include "coro/BlockOn.h"
#include "coro/WhenAll.h"

#include <algorithm>
#include <thread>

namespace imager {

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

MultiDatabase::MultiDatabase(const std::vector<config::TargetConfig>& targets)
    : m_pool(std::max<size_t>(targets.size(), 2u))
{
    m_dbs.reserve(targets.size());
    for (const auto& t : targets)
        m_dbs.push_back(std::make_unique<db::Database>(t.database));
}

MultiDatabase::~MultiDatabase() = default;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

template <typename Op>
std::vector<coro::Task<void>> MultiDatabase::makeTasks(Op& op) {
    std::vector<coro::Task<void>> tasks;
    tasks.reserve(m_dbs.size());
    for (auto& dbPtr : m_dbs) {
        tasks.push_back(
            [](coro::ThreadPool& pool, db::Database& db, Op& op_) -> coro::Task<void> {
                co_await pool.schedule();
                op_(db);
            }(m_pool, *dbPtr, op)
        );
    }
    return tasks;
}

template <typename Op, typename Compensate>
void MultiDatabase::parallelWriteAll(Op&& op, Compensate&& compensate) {
    // Track per-DB success/failure.
    const size_t n = m_dbs.size();
    std::vector<std::exception_ptr> errors(n);
    std::vector<uint8_t>            succeeded(n, 0); // uint8_t avoids vector<bool> proxy issues

    // Run all operations in parallel, catching per-DB exceptions.
    auto runAll = [&]() -> coro::Task<void> {
        std::vector<coro::Task<void>> tasks;
        tasks.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            tasks.push_back(
                [](coro::ThreadPool& pool, db::Database& db,
                   Op& op_, uint8_t& ok, std::exception_ptr& err) -> coro::Task<void> {
                    co_await pool.schedule();
                    try {
                        op_(db);
                        ok = 1;
                    } catch (...) {
                        err = std::current_exception();
                    }
                }(m_pool, *m_dbs[i], op, succeeded[i], errors[i])
            );
        }
        co_await coro::whenAll(std::move(tasks));
    };
    coro::blockOn(m_pool, runAll());

    // Check for failures.
    std::exception_ptr firstError;
    for (auto& ep : errors)
        if (ep && !firstError) firstError = ep;

    if (!firstError) return; // all succeeded

    // Compensate succeeded DBs in parallel.
    auto compensateAll = [&]() -> coro::Task<void> {
        std::vector<coro::Task<void>> tasks;
        tasks.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (!succeeded[i]) continue; // 0 = not succeeded
            tasks.push_back(
                [](coro::ThreadPool& pool, db::Database& db,
                   Compensate& comp_, size_t idx) -> coro::Task<void> {
                    co_await pool.schedule();
                    try { comp_(db, idx); } catch (...) { /* double fault: ignore */ }
                }(m_pool, *m_dbs[i], compensate, i)
            );
        }
        if (!tasks.empty())
            co_await coro::whenAll(std::move(tasks));
    };
    coro::blockOn(m_pool, compensateAll());

    std::rethrow_exception(firstError);
}

template <typename Op>
void MultiDatabase::parallelWriteAll(Op&& op) {
    parallelWriteAll(
        std::forward<Op>(op),
        [](db::Database&, size_t) {} // no-op compensation
    );
}

// ---------------------------------------------------------------------------
// Write operations
// ---------------------------------------------------------------------------

void MultiDatabase::addFile(const std::string& id, const std::string& name,
                             uint64_t size, const std::string& ext) {
    parallelWriteAll(
        [&](db::Database& db) { db.addFile(id, name, size, ext); },
        [&](db::Database& db, size_t) {
            try { db.deleteFile(id); } catch (...) {}
        }
    );
}

void MultiDatabase::deleteFile(const std::string& id) {
    // Capture state before any deletion for rollback.
    auto file = m_dbs[0]->getFile(id);
    if (!file) throw db::DatabaseException(db::DatabaseErrorCode::NotFound,
                                            "File not found: " + id);
    auto tags = m_dbs[0]->getTagsForFile(id);

    parallelWriteAll(
        [&](db::Database& db) { db.deleteFile(id); },
        [&](db::Database& db, size_t) {
            try {
                db.addFile(file->id, file->name, file->size, file->ext);
                for (const auto& tag : tags)
                    try { db.bindTag(id, tag); } catch (...) {}
            } catch (...) {}
        }
    );
}

void MultiDatabase::editFileName(const std::string& id, const std::string& newName) {
    // Capture original name for rollback.
    auto file = m_dbs[0]->getFile(id);
    if (!file) throw db::DatabaseException(db::DatabaseErrorCode::NotFound,
                                            "File not found: " + id);
    const std::string originalName = file->name;

    parallelWriteAll(
        [&](db::Database& db) { db.editFileName(id, newName); },
        [&](db::Database& db, size_t) {
            try { db.editFileName(id, originalName); } catch (...) {}
        }
    );
}

void MultiDatabase::addTag(const std::string& name) {
    parallelWriteAll(
        [&](db::Database& db) { db.addTag(name); },
        [&](db::Database& db, size_t) {
            try { db.deleteTag(name); } catch (...) {}
        }
    );
}

void MultiDatabase::deleteTag(const std::string& name) {
    // Capture all file bindings for rollback.
    std::vector<std::string> boundFiles;
    {
        auto files = m_dbs[0]->getFilesByTags({name});
        for (const auto& f : files)
            boundFiles.push_back(f.id);
    }

    parallelWriteAll(
        [&](db::Database& db) { db.deleteTag(name); },
        [&](db::Database& db, size_t) {
            try {
                db.addTag(name);
                for (const auto& fid : boundFiles)
                    try { db.bindTag(fid, name); } catch (...) {}
            } catch (...) {}
        }
    );
}

void MultiDatabase::bindTag(const std::string& fileId, const std::string& tagName) {
    parallelWriteAll(
        [&](db::Database& db) { db.bindTag(fileId, tagName); },
        [&](db::Database& db, size_t) {
            try { db.unbindTag(fileId, tagName); } catch (...) {}
        }
    );
}

void MultiDatabase::unbindTag(const std::string& fileId, const std::string& tagName) {
    parallelWriteAll(
        [&](db::Database& db) { db.unbindTag(fileId, tagName); },
        [&](db::Database& db, size_t) {
            try { db.bindTag(fileId, tagName); } catch (...) {}
        }
    );
}

// ---------------------------------------------------------------------------
// Read operations (first DB only)
// ---------------------------------------------------------------------------

std::optional<db::File> MultiDatabase::getFile(const std::string& id) {
    return m_dbs[0]->getFile(id);
}

std::vector<db::File> MultiDatabase::getAllFiles(std::optional<db::Pagination> page) {
    return m_dbs[0]->getAllFiles(page);
}

bool MultiDatabase::fileExists(const std::string& id) {
    return m_dbs[0]->fileExists(id);
}

uint64_t MultiDatabase::fileCount() {
    return m_dbs[0]->fileCount();
}

std::vector<std::string> MultiDatabase::getAllTags(std::optional<db::Pagination> page) {
    return m_dbs[0]->getAllTags(page);
}

bool MultiDatabase::tagExists(const std::string& name) {
    return m_dbs[0]->tagExists(name);
}

uint64_t MultiDatabase::tagCount() {
    return m_dbs[0]->tagCount();
}

std::vector<std::string> MultiDatabase::getTagsForFile(
        const std::string& fileId, std::optional<db::Pagination> page) {
    return m_dbs[0]->getTagsForFile(fileId, page);
}

std::vector<db::File> MultiDatabase::getFilesByTags(
        const std::vector<std::string>& tagNames, std::optional<db::Pagination> page) {
    return m_dbs[0]->getFilesByTags(tagNames, page);
}

} // namespace imager
