#include "UserStore.h"

#include <auth/types/AuthError.h>
#include <sqlite3.h>

#include <ctime>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace auth {

// ---------------------------------------------------------------------------
// RAII wrappers (mirror database/)
// ---------------------------------------------------------------------------

namespace {

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

} // namespace

// ---------------------------------------------------------------------------
// SQL constants
// ---------------------------------------------------------------------------

static constexpr std::string_view SQL_PRAGMAS = R"(
    PRAGMA foreign_keys = ON;
    PRAGMA journal_mode = WAL;
    PRAGMA busy_timeout = 5000;
)";

static constexpr std::string_view SQL_CREATE_SCHEMA = R"(
    CREATE TABLE IF NOT EXISTS user (
        login         TEXT PRIMARY KEY NOT NULL,
        full_name     TEXT NOT NULL,
        pw_algo       TEXT NOT NULL,
        pw_iterations INTEGER NOT NULL,
        pw_salt       BLOB NOT NULL,
        pw_hash       BLOB NOT NULL,
        is_admin      INTEGER NOT NULL DEFAULT 0,
        enabled       INTEGER NOT NULL DEFAULT 1,
        created_at    INTEGER NOT NULL,
        updated_at    INTEGER NOT NULL
    );
)";

static constexpr std::string_view SQL_INSERT_USER =
  "INSERT INTO user (login, full_name, pw_algo, pw_iterations, pw_salt, pw_hash, "
  "is_admin, enabled, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?, ?)";

static constexpr std::string_view SQL_DELETE_USER = "DELETE FROM user WHERE login = ?";

static constexpr std::string_view SQL_UPDATE_PASSWORD =
  "UPDATE user SET pw_algo = ?, pw_iterations = ?, pw_salt = ?, pw_hash = ?, updated_at = ? WHERE login = ?";

static constexpr std::string_view SQL_UPDATE_ENABLED = "UPDATE user SET enabled = ?, updated_at = ? WHERE login = ?";

static constexpr std::string_view SQL_UPDATE_ADMIN = "UPDATE user SET is_admin = ?, updated_at = ? WHERE login = ?";

static constexpr std::string_view SQL_SELECT_USER =
  "SELECT login, full_name, is_admin, enabled, created_at, updated_at FROM user WHERE login = ?";

static constexpr std::string_view SQL_SELECT_PASSWORD =
  "SELECT pw_algo, pw_iterations, pw_salt, pw_hash FROM user WHERE login = ?";

static constexpr std::string_view SQL_SELECT_ALL_USERS =
  "SELECT login, full_name, is_admin, enabled, created_at, updated_at FROM user ORDER BY login";

static constexpr std::string_view SQL_SELECT_ALL_USERS_PAGE =
  "SELECT login, full_name, is_admin, enabled, created_at, updated_at FROM user "
  "ORDER BY login LIMIT ? OFFSET ?";

static constexpr std::string_view SQL_COUNT_USERS = "SELECT COUNT(*) FROM user";

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct UserStore::Impl {
  DbPtr db;
  mutable std::shared_mutex mutex;

  explicit Impl(sqlite3* raw)
    : db(raw) {}

  [[nodiscard]] StmtPtr prepare(std::string_view sql) const {
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db.get(), sql.data(), static_cast<int>(sql.size()), &raw, nullptr);
    if (rc != SQLITE_OK) {
      throw AuthException(AuthErrorCode::QueryFailed, std::string("prepare failed: ") + sqlite3_errmsg(db.get()));
    }
    return StmtPtr(raw);
  }

  void execScript(std::string_view sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db.get(), sql.data(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
      std::string msg = errmsg ? errmsg : "unknown error";
      sqlite3_free(errmsg);
      throw AuthException(AuthErrorCode::QueryFailed, "exec failed: " + msg);
    }
  }

  // Step a statement expecting SQLITE_DONE; maps SQLITE_CONSTRAINT to onConstraint.
  void mustDone(sqlite3_stmt* stmt, AuthErrorCode onConstraint, const std::string& constraintMsg) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_CONSTRAINT) {
      throw AuthException(onConstraint, constraintMsg);
    }
    if (rc != SQLITE_DONE) {
      throw AuthException(AuthErrorCode::QueryFailed, std::string("step failed: ") + sqlite3_errmsg(db.get()));
    }
  }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

User rowToUser(sqlite3_stmt* stmt) {
  User u;
  u.login = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  u.fullName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  u.isAdmin = sqlite3_column_int(stmt, 2) != 0;
  u.enabled = sqlite3_column_int(stmt, 3) != 0;
  u.createdAt = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
  u.updatedAt = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
  return u;
}

uint64_t nowEpoch() {
  return static_cast<uint64_t>(std::time(nullptr));
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

UserStore::UserStore(const std::filesystem::path& dbPath) {
  sqlite3* raw = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  const int rc = sqlite3_open_v2(dbPath.string().c_str(), &raw, flags, nullptr);

  if (rc != SQLITE_OK) {
    std::string msg = raw ? sqlite3_errmsg(raw) : "unknown error";
    if (raw) {
      sqlite3_close(raw);
    }
    throw AuthException(AuthErrorCode::OpenFailed, "Failed to open user database: " + msg);
  }

  m_impl = std::make_unique<Impl>(raw);

  try {
    m_impl->execScript(SQL_PRAGMAS);
    m_impl->execScript(SQL_CREATE_SCHEMA);
  } catch (...) {
    m_impl.reset();
    throw;
  }
}

UserStore::~UserStore() = default;
UserStore::UserStore(UserStore&&) noexcept = default;
UserStore& UserStore::operator=(UserStore&&) noexcept = default;

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

void UserStore::create(const std::string& login, const std::string& fullName, const PasswordRecord& pw, bool isAdmin) {
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_INSERT_USER);
  const uint64_t now = nowEpoch();

  sqlite3_bind_text(stmt.get(), 1, login.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 2, fullName.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt.get(), 3, pw.algo.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt.get(), 4, static_cast<int>(pw.iterations));
  sqlite3_bind_blob(stmt.get(), 5, pw.salt.data(), static_cast<int>(pw.salt.size()), SQLITE_STATIC);
  sqlite3_bind_blob(stmt.get(), 6, pw.hash.data(), static_cast<int>(pw.hash.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt.get(), 7, isAdmin ? 1 : 0);
  sqlite3_bind_int64(stmt.get(), 8, static_cast<sqlite3_int64>(now));
  sqlite3_bind_int64(stmt.get(), 9, static_cast<sqlite3_int64>(now));

  m_impl->mustDone(stmt.get(), AuthErrorCode::Duplicate, "User '" + login + "' already exists");
}

void UserStore::remove(const std::string& login) {
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_DELETE_USER);
  sqlite3_bind_text(stmt.get(), 1, login.c_str(), -1, SQLITE_STATIC);
  m_impl->mustDone(stmt.get(), AuthErrorCode::QueryFailed, "remove failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw AuthException(AuthErrorCode::NotFound, "User not found: " + login);
  }
}

void UserStore::setPassword(const std::string& login, const PasswordRecord& pw) {
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_UPDATE_PASSWORD);
  const uint64_t now = nowEpoch();

  sqlite3_bind_text(stmt.get(), 1, pw.algo.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt.get(), 2, static_cast<int>(pw.iterations));
  sqlite3_bind_blob(stmt.get(), 3, pw.salt.data(), static_cast<int>(pw.salt.size()), SQLITE_STATIC);
  sqlite3_bind_blob(stmt.get(), 4, pw.hash.data(), static_cast<int>(pw.hash.size()), SQLITE_STATIC);
  sqlite3_bind_int64(stmt.get(), 5, static_cast<sqlite3_int64>(now));
  sqlite3_bind_text(stmt.get(), 6, login.c_str(), -1, SQLITE_STATIC);

  m_impl->mustDone(stmt.get(), AuthErrorCode::QueryFailed, "setPassword failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw AuthException(AuthErrorCode::NotFound, "User not found: " + login);
  }
}

void UserStore::setEnabled(const std::string& login, bool enabled) {
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_UPDATE_ENABLED);
  const uint64_t now = nowEpoch();

  sqlite3_bind_int(stmt.get(), 1, enabled ? 1 : 0);
  sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(now));
  sqlite3_bind_text(stmt.get(), 3, login.c_str(), -1, SQLITE_STATIC);

  m_impl->mustDone(stmt.get(), AuthErrorCode::QueryFailed, "setEnabled failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw AuthException(AuthErrorCode::NotFound, "User not found: " + login);
  }
}

void UserStore::setAdmin(const std::string& login, bool isAdmin) {
  std::unique_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_UPDATE_ADMIN);
  const uint64_t now = nowEpoch();

  sqlite3_bind_int(stmt.get(), 1, isAdmin ? 1 : 0);
  sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(now));
  sqlite3_bind_text(stmt.get(), 3, login.c_str(), -1, SQLITE_STATIC);

  m_impl->mustDone(stmt.get(), AuthErrorCode::QueryFailed, "setAdmin failed");
  if (sqlite3_changes(m_impl->db.get()) == 0) {
    throw AuthException(AuthErrorCode::NotFound, "User not found: " + login);
  }
}

std::optional<User> UserStore::get(const std::string& login) {
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_USER);
  sqlite3_bind_text(stmt.get(), 1, login.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return rowToUser(stmt.get());
  }
  return std::nullopt;
}

std::optional<PasswordRecord> UserStore::getPassword(const std::string& login) {
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_SELECT_PASSWORD);
  sqlite3_bind_text(stmt.get(), 1, login.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return std::nullopt;
  }

  PasswordRecord pw;
  pw.algo = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
  pw.iterations = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 1));

  const auto* saltBytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt.get(), 2));
  const int saltSize = sqlite3_column_bytes(stmt.get(), 2);
  pw.salt.assign(saltBytes, saltBytes + saltSize);

  const auto* hashBytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt.get(), 3));
  const int hashSize = sqlite3_column_bytes(stmt.get(), 3);
  pw.hash.assign(hashBytes, hashBytes + hashSize);

  return pw;
}

std::vector<User> UserStore::list(std::optional<db::Pagination> page) {
  std::shared_lock lock(m_impl->mutex);
  StmtPtr stmt;
  if (page) {
    stmt = m_impl->prepare(SQL_SELECT_ALL_USERS_PAGE);
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(page->limit));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(page->offset));
  } else {
    stmt = m_impl->prepare(SQL_SELECT_ALL_USERS);
  }

  std::vector<User> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    result.push_back(rowToUser(stmt.get()));
  }
  return result;
}

uint64_t UserStore::count() {
  std::shared_lock lock(m_impl->mutex);
  auto stmt = m_impl->prepare(SQL_COUNT_USERS);
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 0));
  }
  return 0;
}

} // namespace auth
