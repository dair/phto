#include "imager/Imager.h"
#include "imager/ImageValidator.h"

#include "MultiDatabase.h"
#include "Hasher.h"
#include "FileStorage.h"
#include "Validators.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace imager {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Imager::Impl {
    MultiDatabase                                         dbs;
    FileStorage                                           storage;
    std::vector<std::unique_ptr<validation::IValidator>>  validators;
    std::mutex                                            writeMutex;

    explicit Impl(const config::AppConfig& cfg)
        : dbs(cfg.targets)
        , storage(extractRoots(cfg.targets))
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
};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Imager::Imager(const config::AppConfig& cfg)
    : m_impl(std::make_unique<Impl>(cfg)) {}

Imager::~Imager() = default;

// ---------------------------------------------------------------------------
// addImage
// ---------------------------------------------------------------------------

AddResult Imager::addImage(const uint8_t* data, size_t size,
                            const std::string& filename) {
    // 1. Extract & lowercase extension
    std::string ext = Impl::lowercaseExt(filename);
    if (ext.empty())
        return {ErrorCode::UnsupportedFormat, "", "Filename has no extension"};

    // 2. Validate format
    const auto* validator = m_impl->findValidator(ext);
    if (validator) {
        auto res = validator->validate(data, size);
        if (!res.valid)
            return {ErrorCode::BrokenFile, "", res.errorMessage};
    } else if (!Impl::isVideoExtension(ext)) {
        return {ErrorCode::UnsupportedFormat, "",
                "Unsupported format: " + ext};
    }

    // 3. Compute SHA256
    std::string id;
    try {
        id = computeSha256(data, size);
    } catch (const std::exception& e) {
        return {ErrorCode::StorageError, "", std::string("Hashing failed: ") + e.what()};
    }

    std::lock_guard<std::mutex> lock(m_impl->writeMutex);

    // 4. Duplicate check
    try {
        if (m_impl->dbs.fileExists(id))
            return {ErrorCode::DuplicateFile, "", "File already exists: " + id};
    } catch (const db::DatabaseException& e) {
        return {ErrorCode::DatabaseError, "", e.what()};
    }

    // 5. Write to all storage roots
    try {
        m_impl->storage.writeFile(id, ext, data, size);
    } catch (const std::exception& e) {
        return {ErrorCode::StorageError, "",
                std::string("Storage write failed: ") + e.what()};
    }

    // 6. Insert into all databases (parallel)
    try {
        m_impl->dbs.addFile(id, filename, size, ext);
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::ConstraintViolation) {
            return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
        }
        m_impl->storage.deleteFile(id, ext);
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
// getImagesByTags
// ---------------------------------------------------------------------------

std::vector<ImageInfo> Imager::getImagesByTags(
    const std::vector<std::string>& tags,
    uint32_t offset, uint32_t limit) {

    if (tags.empty()) return {};
    try {
        auto files = m_impl->dbs.getFilesByTags(tags, db::Pagination{offset, limit});
        std::vector<ImageInfo> result;
        result.reserve(files.size());
        for (const auto& f : files) {
            auto fileTags = m_impl->dbs.getTagsForFile(f.id);
            result.push_back(Impl::toImageInfo(f, std::move(fileTags)));
        }
        return result;
    } catch (const db::DatabaseException&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// deleteImage
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
        m_impl->dbs.deleteFile(id); // cascades file_tag rows, parallel across DBs
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::NotFound)
            return ErrorCode::FileNotFound;
        return ErrorCode::DatabaseError;
    }

    m_impl->storage.deleteFile(id, ext);
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

std::vector<uint8_t> Imager::getImageData(const std::string& id) {
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
    } catch (const db::DatabaseException& e) {
        if (e.code() == db::DatabaseErrorCode::ConstraintViolation)
            return ErrorCode::DatabaseError;
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
        std::vector<ImageInfo> result;
        result.reserve(files.size());
        for (const auto& f : files) {
            auto tags = m_impl->dbs.getTagsForFile(f.id);
            result.push_back(Impl::toImageInfo(f, std::move(tags)));
        }
        return result;
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
