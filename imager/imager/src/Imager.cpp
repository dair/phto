#include "imager/Imager.h"
#include "imager/ImageValidator.h"

#include "MultiDatabase.h"
#include "Hasher.h"
#include "FileStorage.h"
#include "Validators.h"
#include "coro/BlockOn.h"
#include "coro/ThreadPool.h"
#include "coro/WhenAll.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace imager {

// ---------------------------------------------------------------------------
// Pool sizing
// ---------------------------------------------------------------------------

static size_t defaultPoolSize(size_t numTargets) {
    size_t hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    // At least 4, at least numTargets, at most 16
    return std::clamp(hw, std::max<size_t>(4u, numTargets), size_t{16});
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Imager::Impl {
    coro::ThreadPool                                      pool;
    MultiDatabase                                         dbs;
    FileStorage                                           storage;
    std::vector<std::unique_ptr<validation::IValidator>>  validators;
    std::mutex                                            writeMutex;

    explicit Impl(const config::AppConfig& cfg)
        : pool(defaultPoolSize(cfg.targets.size()))
        , dbs(cfg.targets, pool)
        , storage(extractRoots(cfg.targets), pool)
        , validators(createDefaultValidators())
    {}

    static std::vector<std::filesystem::path> extractRoots(
            const std::vector<config::TargetConfig>& targets) {
        std::vector<std::filesystem::path> roots;
        roots.reserve(targets.size());
        for (const auto& t : targets) roots.push_back(t.root);
        return roots;
    }

    const validation::IValidator* findValidator(const std::string& ext) const {
        for (const auto& v : validators)
            if (v->supportsExtension(ext)) return v.get();
        return nullptr;
    }

    static bool isVideoExtension(const std::string& ext) {
        return ext == ".mp4" || ext == ".mov";
    }

    static std::string lowercaseExt(const std::string& filename) {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos) return {};
        std::string ext = filename.substr(pos);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext;
    }

    static ImageInfo toImageInfo(const db::File& f,
                                  std::vector<std::string> tags = {}) {
        return ImageInfo{f.id, f.name, f.size, f.ext, std::move(tags)};
    }

    /// Fetch tags for a batch of files in parallel and assemble ImageInfo list.
    coro::Task<std::vector<ImageInfo>> enrichWithTags(std::vector<db::File> files);
};

// ---------------------------------------------------------------------------
// Impl::enrichWithTags
// ---------------------------------------------------------------------------

coro::Task<std::vector<ImageInfo>>
Imager::Impl::enrichWithTags(std::vector<db::File> files) {
    if (files.empty()) co_return {};

    // One coroutine per file, each fetches tags from m_dbs[0] on a pool thread.
    std::vector<coro::Task<std::vector<std::string>>> tagTasks;
    tagTasks.reserve(files.size());
    for (const auto& f : files) {
        tagTasks.push_back(
            [](coro::ThreadPool& p, MultiDatabase& dbs_,
               std::string fileId) -> coro::Task<std::vector<std::string>> {
                co_await p.schedule();
                co_return dbs_.getTagsForFile(fileId);
            }(pool, dbs, f.id)
        );
    }

    auto allTags = co_await coro::whenAll(std::move(tagTasks));

    std::vector<ImageInfo> result;
    result.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i)
        result.push_back(toImageInfo(files[i], std::move(allTags[i])));
    co_return result;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Imager::Imager(const config::AppConfig& cfg)
    : m_impl(std::make_unique<Impl>(cfg)) {}

Imager::~Imager() = default;

// ---------------------------------------------------------------------------
// addImage
// ---------------------------------------------------------------------------

AddResult Imager::addImage(const Blob& blob, const std::string& filename) {
    // 1. Extract & lowercase extension
    std::string ext = Impl::lowercaseExt(filename);
    if (ext.empty())
        return {ErrorCode::UnsupportedFormat, "", "Filename has no extension"};

    const auto* validator = m_impl->findValidator(ext);
    if (!validator && !Impl::isVideoExtension(ext))
        return {ErrorCode::UnsupportedFormat, "", "Unsupported format: " + ext};

    // 2+3. Hash (and validate for images) — parallel when validator present
    std::string id;

    if (!validator) {
        // Video: just hash, no validation needed — skip coroutine overhead
        try {
            id = computeSha256(blob);
        } catch (const std::exception& e) {
            return {ErrorCode::StorageError, "", std::string("Hashing failed: ") + e.what()};
        }
    } else {
        // Image: validate and hash in parallel
        validation::ValidationResult valResult;
        try {
            // Outer task runs validate + hash as two concurrent void-tasks
            auto [vr, hid] = coro::blockOn(m_impl->pool, [](
                    coro::ThreadPool& p,
                    const validation::IValidator* v,
                    Blob b) -> coro::Task<std::pair<validation::ValidationResult,
                                                    std::string>> {
                validation::ValidationResult vRes;
                std::string hId;

                std::vector<coro::Task<void>> tasks;

                tasks.push_back([](coro::ThreadPool& p2,
                                   const validation::IValidator* v2, Blob b2,
                                   validation::ValidationResult& out)
                        -> coro::Task<void> {
                    co_await p2.schedule();
                    out = v2->validate(b2.data(), b2.size());
                }(p, v, b, vRes));

                tasks.push_back([](coro::ThreadPool& p2, Blob b2,
                                   std::string& out)
                        -> coro::Task<void> {
                    co_await p2.schedule();
                    out = computeSha256(b2);
                }(p, b, hId));

                co_await coro::whenAll(std::move(tasks));
                co_return std::make_pair(vRes, std::move(hId));
            }(m_impl->pool, validator, blob));

            valResult = vr;
            id        = std::move(hid);
        } catch (const std::exception& e) {
            return {ErrorCode::StorageError, "",
                    std::string("Validation/hashing failed: ") + e.what()};
        }

        if (!valResult.valid)
            return {ErrorCode::BrokenFile, "", valResult.errorMessage};
    }

    std::lock_guard<std::mutex> lock(m_impl->writeMutex);

    // 4. Duplicate check
    try {
        if (m_impl->dbs.fileExists(id))
            return {ErrorCode::DuplicateFile, "", "File already exists: " + id};
    } catch (const db::DatabaseException& e) {
        return {ErrorCode::DatabaseError, "", e.what()};
    }

    // 5. Write to all storage roots in parallel
    try {
        coro::blockOn(m_impl->pool, m_impl->storage.writeFileAsync(id, ext, blob));
    } catch (const std::exception& e) {
        return {ErrorCode::StorageError, "",
                std::string("Storage write failed: ") + e.what()};
    }

    // 6. Insert into all databases in parallel
    try {
        m_impl->dbs.addFile(id, filename, blob.size(), ext);
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::ConstraintViolation)
            return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
        // Roll back storage
        coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(id, ext));
        return {ErrorCode::DatabaseError, "", e.what()};
    }

    return {ErrorCode::Ok, id, ""};
}

// ---------------------------------------------------------------------------
// getImage
// ---------------------------------------------------------------------------

std::optional<ImageInfo> Imager::getImage(const std::string& id) {
    try {
        auto f = m_impl->dbs.getFile(id);
        if (!f) return std::nullopt;
        auto tags = m_impl->dbs.getTagsForFile(id);
        return Impl::toImageInfo(*f, std::move(tags));
    } catch (const db::DatabaseException&) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// getImagesByTags — parallel tag fan-out
// ---------------------------------------------------------------------------

std::vector<ImageInfo> Imager::getImagesByTags(
    const std::vector<std::string>& tags,
    uint32_t offset, uint32_t limit) {

    if (tags.empty()) return {};
    try {
        auto files = m_impl->dbs.getFilesByTags(tags, db::Pagination{offset, limit});
        return coro::blockOn(m_impl->pool, m_impl->enrichWithTags(std::move(files)));
    } catch (const db::DatabaseException&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// deleteImage — parallel storage cleanup after DB delete
// ---------------------------------------------------------------------------

ErrorCode Imager::deleteImage(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_impl->writeMutex);

    std::optional<db::File> file;
    try {
        file = m_impl->dbs.getFile(id);
    } catch (const db::DatabaseException&) {
        return ErrorCode::DatabaseError;
    }

    if (!file) return ErrorCode::FileNotFound;

    const std::string ext = file->ext;

    try {
        m_impl->dbs.deleteFile(id);
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::NotFound)
            return ErrorCode::FileNotFound;
        return ErrorCode::DatabaseError;
    }

    // Storage cleanup in parallel (best-effort)
    coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(id, ext));
    return ErrorCode::Ok;
}

// ---------------------------------------------------------------------------
// Tag operations
// ---------------------------------------------------------------------------

ErrorCode Imager::tagImage(const std::string& id, const std::string& tag) {
    try {
        m_impl->dbs.bindTag(id, tag);
        return ErrorCode::Ok;
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::NotFound)
            return ErrorCode::FileNotFound;
        return ErrorCode::DatabaseError;
    }
}

ErrorCode Imager::untagImage(const std::string& id, const std::string& tag) {
    try {
        m_impl->dbs.unbindTag(id, tag);
        return ErrorCode::Ok;
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::NotFound)
            return ErrorCode::FileNotFound;
        return ErrorCode::DatabaseError;
    }
}

std::vector<std::string> Imager::getImageTags(const std::string& id) {
    try {
        return m_impl->dbs.getTagsForFile(id);
    } catch (const db::DatabaseException&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// getImageData
// ---------------------------------------------------------------------------

Blob Imager::getImageData(const std::string& id) {
    std::optional<db::File> file;
    try {
        file = m_impl->dbs.getFile(id);
    } catch (const db::DatabaseException&) {
        return {};
    }
    if (!file) return {};
    return m_impl->storage.readFile(id, file->ext);
}

// ---------------------------------------------------------------------------
// System-level tag operations
// ---------------------------------------------------------------------------

ErrorCode Imager::createTag(const std::string& name) {
    try {
        m_impl->dbs.addTag(name);
        return ErrorCode::Ok;
    } catch (const db::DatabaseException&) {
        return ErrorCode::DatabaseError;
    }
}

ErrorCode Imager::deleteTag(const std::string& name) {
    try {
        m_impl->dbs.deleteTag(name);
        return ErrorCode::Ok;
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::NotFound)
            return ErrorCode::FileNotFound;
        return ErrorCode::DatabaseError;
    }
}

std::vector<std::string> Imager::listTags(uint32_t offset, uint32_t limit) {
    try {
        return m_impl->dbs.getAllTags(db::Pagination{offset, limit});
    } catch (const db::DatabaseException&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// List / count
// ---------------------------------------------------------------------------

std::vector<ImageInfo> Imager::listImages(uint32_t offset, uint32_t limit) {
    try {
        auto files = m_impl->dbs.getAllFiles(db::Pagination{offset, limit});
        return coro::blockOn(m_impl->pool, m_impl->enrichWithTags(std::move(files)));
    } catch (const db::DatabaseException&) {
        return {};
    }
}

uint64_t Imager::imageCount() {
    try {
        return m_impl->dbs.fileCount();
    } catch (const db::DatabaseException&) {
        return 0;
    }
}

} // namespace imager
