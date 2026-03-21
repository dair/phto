#pragma once

#include "config/Config.h"
#include "database/Database.h"
#include "coro/Task.h"
#include "coro/ThreadPool.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace imager {

/// Owns N db::Database instances (one per target) and fans out every write
/// operation to all of them in parallel using coroutines.
///
/// All write operations are all-or-nothing: if any DB fails, compensation
/// is applied to the already-succeeded DBs and the exception is rethrown.
///
/// All read operations go to m_dbs[0] (the first database).
///
/// The thread pool is provided externally (owned by Imager::Impl) so that
/// all components share a single pool rather than competing with separate ones.
class MultiDatabase {
public:
    explicit MultiDatabase(const std::vector<config::TargetConfig>& targets,
                           coro::ThreadPool& pool);
    ~MultiDatabase();

    MultiDatabase(const MultiDatabase&)            = delete;
    MultiDatabase& operator=(const MultiDatabase&) = delete;

    // --- Write operations (all-or-nothing, parallel across all DBs) ---

    void addFile(const std::string& id, const std::string& name,
                 uint64_t size, const std::string& ext);

    void deleteFile(const std::string& id);

    void editFileName(const std::string& id, const std::string& newName);

    void addTag(const std::string& name);

    void deleteTag(const std::string& name);

    void bindTag(const std::string& fileId, const std::string& tagName);

    void unbindTag(const std::string& fileId, const std::string& tagName);

    // --- Read operations (from first DB only) ---

    std::optional<db::File> getFile(const std::string& id);
    std::vector<db::File>   getAllFiles(std::optional<db::Pagination> page = std::nullopt);
    bool                    fileExists(const std::string& id);
    uint64_t                fileCount();

    std::vector<std::string> getAllTags(std::optional<db::Pagination> page = std::nullopt);
    bool                     tagExists(const std::string& name);
    uint64_t                 tagCount();

    std::vector<std::string> getTagsForFile(
        const std::string&        fileId,
        std::optional<db::Pagination> page = std::nullopt);

    std::vector<db::File> getFilesByTags(
        const std::vector<std::string>& tagNames,
        std::optional<db::Pagination>   page = std::nullopt);

private:
    std::vector<std::unique_ptr<db::Database>> m_dbs;
    coro::ThreadPool&                          m_pool; // not owned — shared with Imager::Impl

    template <typename Op, typename Compensate>
    void parallelWriteAll(Op&& op, Compensate&& compensate);

    template <typename Op>
    void parallelWriteAll(Op&& op);
};

} // namespace imager
