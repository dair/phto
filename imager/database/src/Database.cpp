#include "database/Database.h"

#include <sqlite3.h>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace db {

// ---------------------------------------------------------------------------
// Exception
// ---------------------------------------------------------------------------

DatabaseException::DatabaseException(DatabaseErrorCode code, const std::string& message)
    : std::runtime_error(message), m_code(code) {}

DatabaseErrorCode DatabaseException::code() const noexcept { return m_code; }

// ---------------------------------------------------------------------------
// RAII wrappers
// ---------------------------------------------------------------------------

struct StmtDeleter {
    void operator()(sqlite3_stmt* s) const noexcept { sqlite3_finalize(s); }
};
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

struct DbDeleter {
    void operator()(sqlite3* d) const noexcept { sqlite3_close(d); }
};
using DbPtr = std::unique_ptr<sqlite3, DbDeleter>;

// ---------------------------------------------------------------------------
// SQL constants
// ---------------------------------------------------------------------------

static constexpr std::string_view SQL_PRAGMAS = R"(
    PRAGMA foreign_keys = ON;
    PRAGMA journal_mode = WAL;
    PRAGMA busy_timeout = 5000;
)";

static constexpr std::string_view SQL_CREATE_SCHEMA = R"(
    PRAGMA foreign_keys = ON;
    CREATE TABLE IF NOT EXISTS file (
        id   TEXT PRIMARY KEY NOT NULL,
        name TEXT NOT NULL,
        size INTEGER NOT NULL,
        ext  TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS tag (
        name TEXT PRIMARY KEY NOT NULL
    );
    CREATE TABLE IF NOT EXISTS file_tag (
        file_id  TEXT NOT NULL REFERENCES file(id) ON DELETE CASCADE,
        tag_name TEXT NOT NULL REFERENCES tag(name) ON DELETE CASCADE,
        PRIMARY KEY (file_id, tag_name)
    );
)";

static constexpr std::string_view SQL_INSERT_FILE =
    "INSERT INTO file (id, name, size, ext) VALUES (?, ?, ?, ?)";
static constexpr std::string_view SQL_DELETE_FILE =
    "DELETE FROM file WHERE id = ?";
static constexpr std::string_view SQL_UPDATE_FILE_NAME =
    "UPDATE file SET name = ? WHERE id = ?";
static constexpr std::string_view SQL_SELECT_FILE =
    "SELECT id, name, size, ext FROM file WHERE id = ?";
static constexpr std::string_view SQL_SELECT_ALL_FILES =
    "SELECT id, name, size, ext FROM file ORDER BY id";
static constexpr std::string_view SQL_SELECT_ALL_FILES_PAGE =
    "SELECT id, name, size, ext FROM file ORDER BY id LIMIT ? OFFSET ?";
static constexpr std::string_view SQL_FILE_EXISTS =
    "SELECT 1 FROM file WHERE id = ? LIMIT 1";
static constexpr std::string_view SQL_FILE_COUNT =
    "SELECT COUNT(*) FROM file";

static constexpr std::string_view SQL_INSERT_TAG =
    "INSERT INTO tag (name) VALUES (?)";
static constexpr std::string_view SQL_DELETE_TAG =
    "DELETE FROM tag WHERE name = ?";
static constexpr std::string_view SQL_SELECT_ALL_TAGS =
    "SELECT name FROM tag ORDER BY name";
static constexpr std::string_view SQL_SELECT_ALL_TAGS_PAGE =
    "SELECT name FROM tag ORDER BY name LIMIT ? OFFSET ?";
static constexpr std::string_view SQL_TAG_EXISTS =
    "SELECT 1 FROM tag WHERE name = ? LIMIT 1";
static constexpr std::string_view SQL_TAG_COUNT =
    "SELECT COUNT(*) FROM tag";

static constexpr std::string_view SQL_INSERT_FILE_TAG =
    "INSERT INTO file_tag (file_id, tag_name) VALUES (?, ?)";
static constexpr std::string_view SQL_DELETE_FILE_TAG =
    "DELETE FROM file_tag WHERE file_id = ? AND tag_name = ?";
static constexpr std::string_view SQL_SELECT_TAGS_FOR_FILE =
    "SELECT tag_name FROM file_tag WHERE file_id = ? ORDER BY tag_name";
static constexpr std::string_view SQL_SELECT_TAGS_FOR_FILE_PAGE =
    "SELECT tag_name FROM file_tag WHERE file_id = ? ORDER BY tag_name LIMIT ? OFFSET ?";

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Database::Impl {
    DbPtr                     db;
    mutable std::shared_mutex mutex;

    explicit Impl(sqlite3* raw) : db(raw) {}

    // Prepare a statement; throws QueryFailed on error.
    [[nodiscard]] StmtPtr prepare(std::string_view sql) const {
        sqlite3_stmt* raw = nullptr;
        int rc = sqlite3_prepare_v2(
            db.get(), sql.data(), static_cast<int>(sql.size()), &raw, nullptr);
        if (rc != SQLITE_OK) {
            throw DatabaseException(
                DatabaseErrorCode::QueryFailed,
                std::string("prepare failed: ") + sqlite3_errmsg(db.get()));
        }
        return StmtPtr(raw);
    }

    // Execute one or more semicolon-separated statements (no results).
    void execScript(std::string_view sql) {
        char* errmsg = nullptr;
        int rc = sqlite3_exec(db.get(), sql.data(), nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string msg = errmsg ? errmsg : "unknown error";
            sqlite3_free(errmsg);
            throw DatabaseException(DatabaseErrorCode::QueryFailed,
                                    "exec failed: " + msg);
        }
    }

    // Step a statement that must produce SQLITE_DONE; throws on any other code.
    void mustDone(sqlite3_stmt* stmt, DatabaseErrorCode onConstraint,
                  const std::string& constraintMsg) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_CONSTRAINT) {
            throw DatabaseException(onConstraint, constraintMsg);
        }
        if (rc != SQLITE_DONE) {
            throw DatabaseException(DatabaseErrorCode::QueryFailed,
                std::string("step failed: ") + sqlite3_errmsg(db.get()));
        }
    }
};

// ---------------------------------------------------------------------------
// Helpers (file row extraction)
// ---------------------------------------------------------------------------

static File rowToFile(sqlite3_stmt* stmt) {
    File f;
    f.id   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    f.size = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
    f.ext  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    return f;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Database::Database(const std::filesystem::path& dbPath) {
    const bool existed = std::filesystem::exists(dbPath);

    sqlite3* raw   = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                    | SQLITE_OPEN_FULLMUTEX;
    const int rc = sqlite3_open_v2(dbPath.string().c_str(), &raw, flags, nullptr);

    if (rc != SQLITE_OK) {
        std::string msg = raw ? sqlite3_errmsg(raw) : "unknown error";
        if (raw) sqlite3_close(raw);
        throw DatabaseException(
            existed ? DatabaseErrorCode::OpenFailed
                    : DatabaseErrorCode::CreationFailed,
            (existed ? "Failed to open database: " : "Failed to create database: ") + msg);
    }

    m_impl = std::make_unique<Impl>(raw);

    try {
        // Apply pragmas as a script (journal_mode returns a result row so we
        // use execScript which ignores results).
        m_impl->execScript(SQL_PRAGMAS);
        // Initialise schema (IF NOT EXISTS guards make this safe on reopen).
        m_impl->execScript(SQL_CREATE_SCHEMA);
    } catch (...) {
        m_impl.reset();
        throw;
    }
}

Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

void Database::addFile(const std::string& id, const std::string& name,
                       uint64_t size, const std::string& ext) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_INSERT_FILE);
    sqlite3_bind_text (stmt.get(), 1, id.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt.get(), 2, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt.get(), 3, static_cast<sqlite3_int64>(size));
    sqlite3_bind_text (stmt.get(), 4, ext.c_str(),  -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(),
        DatabaseErrorCode::ConstraintViolation,
        "File with id '" + id + "' already exists");
}

void Database::deleteFile(const std::string& id) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_DELETE_FILE);
    sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "deleteFile failed");
    if (sqlite3_changes(m_impl->db.get()) == 0)
        throw DatabaseException(DatabaseErrorCode::NotFound, "File not found: " + id);
}

void Database::editFileName(const std::string& id, const std::string& newName) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_UPDATE_FILE_NAME);
    sqlite3_bind_text(stmt.get(), 1, newName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, id.c_str(),      -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "editFileName failed");
    if (sqlite3_changes(m_impl->db.get()) == 0)
        throw DatabaseException(DatabaseErrorCode::NotFound, "File not found: " + id);
}

std::optional<File> Database::getFile(const std::string& id) {
    std::shared_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_SELECT_FILE);
    sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW)
        return rowToFile(stmt.get());
    return std::nullopt;
}

std::vector<File> Database::getAllFiles(std::optional<Pagination> page) {
    std::shared_lock lock(m_impl->mutex);
    StmtPtr stmt;
    if (page) {
        stmt = m_impl->prepare(SQL_SELECT_ALL_FILES_PAGE);
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(page->limit));
        sqlite3_bind_int(stmt.get(), 2, static_cast<int>(page->offset));
    } else {
        stmt = m_impl->prepare(SQL_SELECT_ALL_FILES);
    }
    std::vector<File> result;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result.push_back(rowToFile(stmt.get()));
    return result;
}

bool Database::fileExists(const std::string& id) {
    std::shared_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_FILE_EXISTS);
    sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
    return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

uint64_t Database::fileCount() {
    std::shared_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_FILE_COUNT);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW)
        return static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 0));
    return 0;
}

// ---------------------------------------------------------------------------
// Tag operations
// ---------------------------------------------------------------------------

void Database::addTag(const std::string& name) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_INSERT_TAG);
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(),
        DatabaseErrorCode::ConstraintViolation,
        "Tag '" + name + "' already exists");
}

void Database::deleteTag(const std::string& name) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_DELETE_TAG);
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "deleteTag failed");
    if (sqlite3_changes(m_impl->db.get()) == 0)
        throw DatabaseException(DatabaseErrorCode::NotFound, "Tag not found: " + name);
}

std::vector<std::string> Database::getAllTags(std::optional<Pagination> page) {
    std::shared_lock lock(m_impl->mutex);
    StmtPtr stmt;
    if (page) {
        stmt = m_impl->prepare(SQL_SELECT_ALL_TAGS_PAGE);
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(page->limit));
        sqlite3_bind_int(stmt.get(), 2, static_cast<int>(page->offset));
    } else {
        stmt = m_impl->prepare(SQL_SELECT_ALL_TAGS);
    }
    std::vector<std::string> result;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0)));
    return result;
}

bool Database::tagExists(const std::string& name) {
    std::shared_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_TAG_EXISTS);
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

uint64_t Database::tagCount() {
    std::shared_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_TAG_COUNT);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW)
        return static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 0));
    return 0;
}

// ---------------------------------------------------------------------------
// Association operations
// ---------------------------------------------------------------------------

void Database::bindTag(const std::string& fileId, const std::string& tagName) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_INSERT_FILE_TAG);
    sqlite3_bind_text(stmt.get(), 1, fileId.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, tagName.c_str(), -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(),
        DatabaseErrorCode::ConstraintViolation,
        "bindTag: already bound or file/tag does not exist (file='" + fileId
            + "', tag='" + tagName + "')");
}

void Database::unbindTag(const std::string& fileId, const std::string& tagName) {
    std::unique_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(SQL_DELETE_FILE_TAG);
    sqlite3_bind_text(stmt.get(), 1, fileId.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, tagName.c_str(), -1, SQLITE_STATIC);
    m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "unbindTag failed");
    if (sqlite3_changes(m_impl->db.get()) == 0)
        throw DatabaseException(DatabaseErrorCode::NotFound,
            "Binding not found: file='" + fileId + "', tag='" + tagName + "'");
}

std::vector<std::string> Database::getTagsForFile(const std::string& fileId,
                                                   std::optional<Pagination> page) {
    std::shared_lock lock(m_impl->mutex);
    StmtPtr stmt;
    if (page) {
        stmt = m_impl->prepare(SQL_SELECT_TAGS_FOR_FILE_PAGE);
        sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt.get(), 2, static_cast<int>(page->limit));
        sqlite3_bind_int (stmt.get(), 3, static_cast<int>(page->offset));
    } else {
        stmt = m_impl->prepare(SQL_SELECT_TAGS_FOR_FILE);
        sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
    }
    std::vector<std::string> result;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0)));
    return result;
}

std::vector<File> Database::getFilesByTags(const std::vector<std::string>& tagNames,
                                            std::optional<Pagination> page) {
    if (tagNames.empty()) return {};

    // Build: SELECT files that carry ALL listed tags (intersection semantics).
    std::string placeholders;
    placeholders.reserve(tagNames.size() * 2);
    for (size_t i = 0; i < tagNames.size(); ++i) {
        if (i > 0) placeholders += ',';
        placeholders += '?';
    }

    std::string sql =
        "SELECT f.id, f.name, f.size, f.ext "
        "FROM file f "
        "JOIN file_tag ft ON f.id = ft.file_id "
        "WHERE ft.tag_name IN (" + placeholders + ") "
        "GROUP BY f.id "
        "HAVING COUNT(DISTINCT ft.tag_name) = ? "
        "ORDER BY f.id";

    if (page) sql += " LIMIT ? OFFSET ?";

    std::shared_lock lock(m_impl->mutex);
    auto stmt = m_impl->prepare(sql);

    int idx = 1;
    for (const auto& tag : tagNames)
        sqlite3_bind_text(stmt.get(), idx++, tag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), idx++, static_cast<int>(tagNames.size()));
    if (page) {
        sqlite3_bind_int(stmt.get(), idx++, static_cast<int>(page->limit));
        sqlite3_bind_int(stmt.get(), idx++, static_cast<int>(page->offset));
    }

    std::vector<File> result;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result.push_back(rowToFile(stmt.get()));
    return result;
}

} // namespace db
