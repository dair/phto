#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace db {

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

enum class DatabaseErrorCode {
    CreationFailed,     ///< New database file could not be created
    OpenFailed,         ///< Existing database file could not be opened
    QueryFailed,        ///< A SQL statement failed to execute
    NotFound,           ///< Requested entity does not exist
    ConstraintViolation ///< Unique/FK constraint was violated
};

class DatabaseException : public std::runtime_error {
public:
    DatabaseException(DatabaseErrorCode code, const std::string& message);
    DatabaseErrorCode code() const noexcept;

private:
    DatabaseErrorCode m_code;
};

// ---------------------------------------------------------------------------
// Supporting types
// ---------------------------------------------------------------------------

struct File {
    std::string id;
    std::string name;
    uint64_t    size{0}; ///< File size in bytes; values up to 2^63-1 are exact
    std::string ext;
};

struct Pagination {
    uint32_t offset{0};
    uint32_t limit{50};
};

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

class Database {
public:
    /// Open or create the database at @p dbPath.
    /// - If the path does not exist a new database is created and the schema
    ///   is initialised; throws DatabaseErrorCode::CreationFailed on failure.
    /// - If the path exists it is opened for read-write access; throws
    ///   DatabaseErrorCode::OpenFailed on failure.
    explicit Database(const std::filesystem::path& dbPath);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    // ---- File operations ------------------------------------------------

    /// Insert a new file record.  Throws ConstraintViolation if id exists.
    void addFile(const std::string& id,
                 const std::string& name,
                 uint64_t           size,
                 const std::string& ext);

    /// Remove a file and all its tag bindings.  Throws NotFound if absent.
    void deleteFile(const std::string& id);

    /// Rename a file.  Throws NotFound if the file does not exist.
    void editFileName(const std::string& id, const std::string& newName);

    /// Fetch a single file by id; returns nullopt when not found.
    std::optional<File> getFile(const std::string& id);

    /// Return all files, optionally paginated.
    std::vector<File> getAllFiles(std::optional<Pagination> page = std::nullopt);

    /// Check whether a file with this id exists.
    bool fileExists(const std::string& id);

    /// Return total number of files stored.
    uint64_t fileCount();

    // ---- Tag operations -------------------------------------------------

    /// Create a new tag.  Throws ConstraintViolation if name exists.
    void addTag(const std::string& name);

    /// Delete a tag and all its bindings.  Throws NotFound if absent.
    void deleteTag(const std::string& name);

    /// Return all tags, optionally paginated.
    std::vector<std::string> getAllTags(std::optional<Pagination> page = std::nullopt);

    /// Check whether a tag with this name exists.
    bool tagExists(const std::string& name);

    /// Return total number of tags stored.
    uint64_t tagCount();

    // ---- Association operations -----------------------------------------

    /// Associate @p tagName with @p fileId.
    /// Throws ConstraintViolation if already bound or if either entity is missing.
    void bindTag(const std::string& fileId, const std::string& tagName);

    /// Remove the association between @p tagName and @p fileId.
    /// Throws NotFound if the binding does not exist.
    void unbindTag(const std::string& fileId, const std::string& tagName);

    /// Return all tags associated with @p fileId, optionally paginated.
    std::vector<std::string> getTagsForFile(const std::string&      fileId,
                                            std::optional<Pagination> page = std::nullopt);

    /// Return all files that carry every tag in @p tagNames (AND semantics),
    /// optionally paginated.
    std::vector<File> getFilesByTags(const std::vector<std::string>& tagNames,
                                     std::optional<Pagination>        page = std::nullopt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace db
