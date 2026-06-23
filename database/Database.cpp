#include "Database.h"

#include <metrics/Metrics.h>
#include <metrics/Timer.h>
#include <sqlite3.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>

namespace db {

// ---------------------------------------------------------------------------
// Exception
// ---------------------------------------------------------------------------

DatabaseException::DatabaseException(DatabaseErrorCode code, const std::string& message)
  : std::runtime_error(message),
    m_code(code) {}

DatabaseErrorCode DatabaseException::code() const noexcept {
  return m_code;
}

// ---------------------------------------------------------------------------
// RAII wrappers
// ---------------------------------------------------------------------------

struct StmtDeleter {
  void operator()(sqlite3_stmt* s) const noexcept {
    sqlite3_finalize(s);
  }
};

using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

struct DbDeleter {
  void operator()(sqlite3* d) const noexcept {
    sqlite3_close(d);
  }
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
    CREATE TABLE IF NOT EXISTS original_name (
        source_dir TEXT NOT NULL,
        base_name  TEXT NOT NULL,
        file_id    TEXT NOT NULL REFERENCES file(id) ON DELETE CASCADE,
        PRIMARY KEY (source_dir, base_name, file_id)
    );
    CREATE INDEX IF NOT EXISTS idx_original_name_pairing
        ON original_name(source_dir, base_name);
    CREATE TABLE IF NOT EXISTS file_companion (
        file_id    TEXT PRIMARY KEY NOT NULL REFERENCES file(id) ON DELETE CASCADE,
        parent_id  TEXT REFERENCES file(id) ON DELETE SET NULL,
        storage_id TEXT NOT NULL
    );
)";

static constexpr std::string_view SQL_INSERT_FILE = "INSERT INTO file (id, name, size, ext) VALUES (?, ?, ?, ?)";
static constexpr std::string_view SQL_DELETE_FILE = "DELETE FROM file WHERE id = ?";
static constexpr std::string_view SQL_UPDATE_FILE_NAME = "UPDATE file SET name = ? WHERE id = ?";
static constexpr std::string_view SQL_SELECT_FILE = "SELECT id, name, size, ext FROM file WHERE id = ?";
static constexpr std::string_view SQL_SELECT_ALL_FILES = "SELECT id, name, size, ext FROM file ORDER BY id";
static constexpr std::string_view SQL_SELECT_ALL_FILES_PAGE =
  "SELECT id, name, size, ext FROM file ORDER BY id LIMIT ? OFFSET ?";
static constexpr std::string_view SQL_FILE_EXISTS = "SELECT 1 FROM file WHERE id = ? LIMIT 1";
static constexpr std::string_view SQL_FILE_COUNT = "SELECT COUNT(*) FROM file";

static constexpr std::string_view SQL_INSERT_TAG = "INSERT INTO tag (name) VALUES (?)";
static constexpr std::string_view SQL_DELETE_TAG = "DELETE FROM tag WHERE name = ?";
static constexpr std::string_view SQL_SELECT_ALL_TAGS = "SELECT name FROM tag ORDER BY name";
static constexpr std::string_view SQL_SELECT_ALL_TAGS_PAGE = "SELECT name FROM tag ORDER BY name LIMIT ? OFFSET ?";
static constexpr std::string_view SQL_TAG_EXISTS = "SELECT 1 FROM tag WHERE name = ? LIMIT 1";
static constexpr std::string_view SQL_TAG_COUNT = "SELECT COUNT(*) FROM tag";

static constexpr std::string_view SQL_INSERT_FILE_TAG = "INSERT INTO file_tag (file_id, tag_name) VALUES (?, ?)";
static constexpr std::string_view SQL_INSERT_TAG_OR_IGNORE = "INSERT OR IGNORE INTO tag (name) VALUES (?)";
static constexpr std::string_view SQL_DELETE_ALL_FILE_TAGS_FOR_FILE = "DELETE FROM file_tag WHERE file_id = ?";
static constexpr std::string_view SQL_DELETE_FILE_TAG = "DELETE FROM file_tag WHERE file_id = ? AND tag_name = ?";
static constexpr std::string_view SQL_SELECT_TAGS_FOR_FILE =
  "SELECT tag_name FROM file_tag WHERE file_id = ? ORDER BY tag_name";
static constexpr std::string_view SQL_SELECT_TAGS_FOR_FILE_PAGE =
  "SELECT tag_name FROM file_tag WHERE file_id = ? ORDER BY tag_name LIMIT ? OFFSET ?";

// original_name
static constexpr std::string_view SQL_INSERT_ORIGINAL_NAME =
  "INSERT OR IGNORE INTO original_name (source_dir, base_name, file_id) VALUES (?, ?, ?)";
static constexpr std::string_view SQL_SELECT_FILES_BY_SOURCE_BASENAME =
  "SELECT f.id, f.name, f.size, f.ext "
  "FROM file f "
  "JOIN original_name on_ ON on_.file_id = f.id "
  "WHERE on_.source_dir = ? AND on_.base_name = ?";

// untagged files
static constexpr std::string_view SQL_SELECT_UNTAGGED_FILES =
  "SELECT id, name, size, ext FROM file WHERE id NOT IN (SELECT file_id FROM file_tag) ORDER BY id";
static constexpr std::string_view SQL_SELECT_UNTAGGED_FILES_PAGE =
  "SELECT id, name, size, ext FROM file WHERE id NOT IN (SELECT file_id FROM file_tag) ORDER BY id LIMIT ? OFFSET ?";

// file_companion
static constexpr std::string_view SQL_INSERT_COMPANION =
  "INSERT INTO file_companion (file_id, parent_id, storage_id) VALUES (?, ?, ?)";
static constexpr std::string_view SQL_SELECT_COMPANION =
  "SELECT file_id, parent_id, storage_id FROM file_companion WHERE file_id = ?";
static constexpr std::string_view SQL_SELECT_ORPHAN_COMPANIONS_BY_SOURCE_BASENAME =
  "SELECT fc.file_id, fc.parent_id, fc.storage_id "
  "FROM file_companion fc "
  "JOIN original_name on_ ON on_.file_id = fc.file_id "
  "WHERE fc.parent_id IS NULL "
  "AND on_.source_dir = ? AND on_.base_name = ?";
static constexpr std::string_view SQL_UPDATE_COMPANION_PARENT =
  "UPDATE file_companion SET parent_id = ?, storage_id = ? WHERE file_id = ?";
static constexpr std::string_view SQL_SELECT_COMPANIONS_FOR_PARENT =
  "SELECT file_id, parent_id, storage_id FROM file_companion WHERE parent_id = ?";

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Database::Impl {
  DbPtr db;
  mutable std::shared_mutex mutex;
  metrics::Metrics* metrics{nullptr};

  explicit Impl(sqlite3* raw, metrics::Metrics* m)
    : db(raw),
      metrics(m) {}

  [[nodiscard]] std::optional<metrics::Timer> readTimer() const {
    if (metrics) {
      return std::optional<metrics::Timer>{std::in_place, metrics->db_read_duration};
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<metrics::Timer> writeTimer() const {
    if (metrics) {
      return std::optional<metrics::Timer>{std::in_place, metrics->db_write_duration};
    }
    return std::nullopt;
  }

  // Prepare a statement; throws QueryFailed on error.
  [[nodiscard]] StmtPtr prepare(std::string_view sql) const {
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db.get(), sql.data(), static_cast<int>(sql.size()), &raw, nullptr);
    if (rc != SQLITE_OK) {
      throw DatabaseException(
        DatabaseErrorCode::QueryFailed, std::string("prepare failed: ") + sqlite3_errmsg(db.get())
      );
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
      throw DatabaseException(DatabaseErrorCode::QueryFailed, "exec failed: " + msg);
    }
  }

  // Step a statement that must produce SQLITE_DONE; throws on any other code.
  void mustDone(sqlite3_stmt* stmt, DatabaseErrorCode onConstraint, const std::string& constraintMsg) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_CONSTRAINT) {
      throw DatabaseException(onConstraint, constraintMsg);
    }
    if (rc != SQLITE_DONE) {
      throw DatabaseException(DatabaseErrorCode::QueryFailed, std::string("step failed: ") + sqlite3_errmsg(db.get()));
    }
  }
};

// ---------------------------------------------------------------------------
// Helpers (file row extraction)
// ---------------------------------------------------------------------------

static File rowToFile(sqlite3_stmt* stmt) {
  File f;
  f.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  f.size = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
  f.ext = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
  return f;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Database::Database(const std::filesystem::path& dbPath, metrics::Metrics* metrics) {
  const bool existed = std::filesystem::exists(dbPath);

  sqlite3* raw = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  const int rc = sqlite3_open_v2(dbPath.string().c_str(), &raw, flags, nullptr);

  if (rc != SQLITE_OK) {
    std::string msg = raw ? sqlite3_errmsg(raw) : "unknown error";
    if (raw) {
      sqlite3_close(raw);
    }
    throw DatabaseException(
      existed ? DatabaseErrorCode::OpenFailed : DatabaseErrorCode::CreationFailed,
      (existed ? "Failed to open database: " : "Failed to create database: ") + msg
    );
  }

  m_impl = std::make_unique<Impl>(raw, metrics);

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

void Database::addFile(const std::string& id, const std::string& name, uint64_t size, const std::string& ext) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_INSERT_FILE);
  sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, name.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt.get(), 3, static_cast<sqlite3_int64>(size));
  sqlite3_bind_text(stmt.get(), 4, ext.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::ConstraintViolation, "File with id '" + id + "' already exists");
}

void Database::deleteFile(const std::string& id) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_DELETE_FILE);
  sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "deleteFile failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw DatabaseException(DatabaseErrorCode::NotFound, "File not found: " + id);
  }
}

void Database::editFileName(const std::string& id, const std::string& newName) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_UPDATE_FILE_NAME);
  sqlite3_bind_text(stmt.get(), 1, newName.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, id.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "editFileName failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw DatabaseException(DatabaseErrorCode::NotFound, "File not found: " + id);
  }
}

std::optional<File> Database::getFile(const std::string& id) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_FILE);
  sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return rowToFile(stmt.get());
  }
  return std::nullopt;
}

std::vector<File> Database::getAllFiles(std::optional<Pagination> page) {
  auto t = m_impl->readTimer();
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
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.push_back(rowToFile(stmt.get()));
  }
  return result;
}

bool Database::fileExists(const std::string& id) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_FILE_EXISTS);
  sqlite3_bind_text(stmt.get(), 1, id.c_str(), -1, SQLITE_STATIC);
  return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

uint64_t Database::fileCount() {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_FILE_COUNT);
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 0));
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Tag operations
// ---------------------------------------------------------------------------

void Database::addTag(const std::string& name) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_INSERT_TAG);
  sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::ConstraintViolation, "Tag '" + name + "' already exists");
}

void Database::deleteTag(const std::string& name) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_DELETE_TAG);
  sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "deleteTag failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw DatabaseException(DatabaseErrorCode::NotFound, "Tag not found: " + name);
  }
}

std::vector<std::string> Database::getAllTags(std::optional<Pagination> page) {
  auto t = m_impl->readTimer();
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
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0)));
  }
  return result;
}

bool Database::tagExists(const std::string& name) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_TAG_EXISTS);
  sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
  return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

uint64_t Database::tagCount() {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_TAG_COUNT);
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 0));
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Association operations
// ---------------------------------------------------------------------------

void Database::bindTag(const std::string& fileId, const std::string& tagName) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_INSERT_FILE_TAG);
  sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, tagName.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(
    stmt.get(),
    DatabaseErrorCode::ConstraintViolation,
    "bindTag: already bound or file/tag does not exist (file='" + fileId + "', tag='" + tagName + "')"
  );
}

void Database::unbindTag(const std::string& fileId, const std::string& tagName) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_DELETE_FILE_TAG);
  sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, tagName.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "unbindTag failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw DatabaseException(
      DatabaseErrorCode::NotFound, "Binding not found: file='" + fileId + "', tag='" + tagName + "'"
    );
  }
}

std::vector<std::string> Database::getTagsForFile(const std::string& fileId, std::optional<Pagination> page) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  StmtPtr stmt;
  if (page) {
    stmt = m_impl->prepare(SQL_SELECT_TAGS_FOR_FILE_PAGE);
    sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(page->limit));
    sqlite3_bind_int(stmt.get(), 3, static_cast<int>(page->offset));
  } else {
    stmt = m_impl->prepare(SQL_SELECT_TAGS_FOR_FILE);
    sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
  }
  std::vector<std::string> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0)));
  }
  return result;
}

std::vector<File> Database::getUntaggedFiles(std::optional<Pagination> page) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  StmtPtr stmt;
  if (page) {
    stmt = m_impl->prepare(SQL_SELECT_UNTAGGED_FILES_PAGE);
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(page->limit));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(page->offset));
  } else {
    stmt = m_impl->prepare(SQL_SELECT_UNTAGGED_FILES);
  }
  std::vector<File> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.push_back(rowToFile(stmt.get()));
  }
  return result;
}

void Database::setTagsForFile(const std::string& fileId, const std::vector<std::string>& tags) {
  auto t = m_impl->writeTimer();

  // De-duplicate while preserving order (sorted unique).
  std::vector<std::string> unique_tags = tags;
  std::sort(unique_tags.begin(), unique_tags.end());
  unique_tags.erase(std::unique(unique_tags.begin(), unique_tags.end()), unique_tags.end());

  std::unique_lock lock(m_impl->mutex);

  // Single transaction: delete existing bindings, upsert tags, insert new bindings.
  m_impl->execScript("BEGIN");
  try {
    // 1. Delete all existing file_tag rows for this file.
    {
      auto del = m_impl->prepare(SQL_DELETE_ALL_FILE_TAGS_FOR_FILE);
      sqlite3_bind_text(del.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
      m_impl->mustDone(del.get(), DatabaseErrorCode::QueryFailed, "setTagsForFile: delete failed");
    }

    // 2. For each tag: ensure it exists, then bind it.
    for (const auto& tagName : unique_tags) {
      {
        auto ins = m_impl->prepare(SQL_INSERT_TAG_OR_IGNORE);
        sqlite3_bind_text(ins.get(), 1, tagName.c_str(), -1, SQLITE_STATIC);
        m_impl->mustDone(ins.get(), DatabaseErrorCode::QueryFailed, "setTagsForFile: insert tag failed");
      }
      {
        auto bind = m_impl->prepare(SQL_INSERT_FILE_TAG);
        sqlite3_bind_text(bind.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(bind.get(), 2, tagName.c_str(), -1, SQLITE_STATIC);
        m_impl->mustDone(
          bind.get(), DatabaseErrorCode::ConstraintViolation, "setTagsForFile: bind failed for tag '" + tagName + "'"
        );
      }
    }

    m_impl->execScript("COMMIT");
  } catch (...) {
    try {
      m_impl->execScript("ROLLBACK");
    } catch (...) {}
    throw;
  }
}

// ---------------------------------------------------------------------------
// Original name operations
// ---------------------------------------------------------------------------

void Database::addOriginalName(const std::string& sourceDir, const std::string& baseName, const std::string& fileId) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_INSERT_ORIGINAL_NAME);
  sqlite3_bind_text(stmt.get(), 1, sourceDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, baseName.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 3, fileId.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "addOriginalName failed");
}

std::vector<File> Database::getFilesBySourceAndBaseName(const std::string& sourceDir, const std::string& baseName) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_FILES_BY_SOURCE_BASENAME);
  sqlite3_bind_text(stmt.get(), 1, sourceDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, baseName.c_str(), -1, SQLITE_STATIC);
  std::vector<File> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.push_back(rowToFile(stmt.get()));
  }
  return result;
}

// ---------------------------------------------------------------------------
// Companion (sidecar) operations
// ---------------------------------------------------------------------------

void Database::addCompanion(
  const std::string& fileId, const std::optional<std::string>& parentId, const std::string& storageId
) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_INSERT_COMPANION);
  sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
  if (parentId) {
    sqlite3_bind_text(stmt.get(), 2, parentId->c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt.get(), 2);
  }
  sqlite3_bind_text(stmt.get(), 3, storageId.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(
    stmt.get(),
    DatabaseErrorCode::ConstraintViolation,
    "addCompanion: companion already exists for file '" + fileId + "'"
  );
}

std::optional<Database::CompanionInfo> Database::getCompanion(const std::string& fileId) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_COMPANION);
  sqlite3_bind_text(stmt.get(), 1, fileId.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return std::nullopt;
  }
  CompanionInfo info;
  info.fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
  if (sqlite3_column_type(stmt.get(), 1) != SQLITE_NULL) {
    info.parentId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
  }
  info.storageId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
  return info;
}

std::vector<Database::CompanionInfo> Database::getOrphanCompanionsBySourceAndBaseName(
  const std::string& sourceDir, const std::string& baseName
) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_ORPHAN_COMPANIONS_BY_SOURCE_BASENAME);
  sqlite3_bind_text(stmt.get(), 1, sourceDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, baseName.c_str(), -1, SQLITE_STATIC);
  std::vector<CompanionInfo> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    CompanionInfo info;
    info.fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    // parent_id is NULL for orphans, so skip column 1
    info.storageId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
    result.push_back(std::move(info));
  }
  return result;
}

void Database::updateCompanionParent(
  const std::string& fileId, const std::string& parentId, const std::string& storageId
) {
  auto t = m_impl->writeTimer();
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_UPDATE_COMPANION_PARENT);
  sqlite3_bind_text(stmt.get(), 1, parentId.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, storageId.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 3, fileId.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), DatabaseErrorCode::QueryFailed, "updateCompanionParent failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw DatabaseException(DatabaseErrorCode::NotFound, "Companion not found for file: " + fileId);
  }
}

std::vector<Database::CompanionInfo> Database::getCompanionsForParent(const std::string& parentId) {
  auto t = m_impl->readTimer();
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_COMPANIONS_FOR_PARENT);
  sqlite3_bind_text(stmt.get(), 1, parentId.c_str(), -1, SQLITE_STATIC);
  std::vector<CompanionInfo> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    CompanionInfo info;
    info.fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    if (sqlite3_column_type(stmt.get(), 1) != SQLITE_NULL) {
      info.parentId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
    }
    info.storageId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
    result.push_back(std::move(info));
  }
  return result;
}

std::vector<File> Database::getFilesByTags(const std::vector<std::string>& tagNames, std::optional<Pagination> page) {
  if (tagNames.empty()) {
    return {};
  }
  auto t = m_impl->readTimer();

  // Build: SELECT files that carry ALL listed tags (intersection semantics).
  std::string placeholders;
  placeholders.reserve(tagNames.size() * 2);
  for (size_t i = 0; i < tagNames.size(); ++i) {
    if (i > 0) {
      placeholders += ',';
    }
    placeholders += '?';
  }

  std::string sql =
    "SELECT f.id, f.name, f.size, f.ext "
    "FROM file f "
    "JOIN file_tag ft ON f.id = ft.file_id "
    "WHERE ft.tag_name IN (" +
    placeholders +
    ") "
    "GROUP BY f.id "
    "HAVING COUNT(DISTINCT ft.tag_name) = ? "
    "ORDER BY f.id";

  if (page) {
    sql += " LIMIT ? OFFSET ?";
  }

  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(sql);

  int idx = 1;
  for (const auto& tag : tagNames) {
    sqlite3_bind_text(stmt.get(), idx++, tag.c_str(), -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_int(stmt.get(), idx++, static_cast<int>(tagNames.size()));
  if (page) {
    sqlite3_bind_int(stmt.get(), idx++, static_cast<int>(page->limit));
    sqlite3_bind_int(stmt.get(), idx++, static_cast<int>(page->offset));
  }

  std::vector<File> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.push_back(rowToFile(stmt.get()));
  }
  return result;
}

} // namespace db
