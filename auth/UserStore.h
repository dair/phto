#pragma once

#include <auth/PasswordHash.h>
#include <auth/types/User.h>
#include <database/Database.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace auth {

/// Partial user update; unset fields are left unchanged.
struct UserUpdate {
  std::optional<std::string> fullName;
  std::optional<PasswordRecord> password;
  std::optional<bool> isAdmin;
  std::optional<bool> enabled;
};

class UserStore {
public:
  explicit UserStore(const std::filesystem::path& dbPath);
  ~UserStore();

  UserStore(const UserStore&) = delete;
  UserStore& operator=(const UserStore&) = delete;
  UserStore(UserStore&&) noexcept;
  UserStore& operator=(UserStore&&) noexcept;

  /// Insert a new user. Throws AuthException(Duplicate) if login already exists.
  void create(const std::string& login, const std::string& fullName, const PasswordRecord& pw, bool isAdmin);

  /// Replace stored password. Throws AuthException(NotFound) if login absent.
  void setPassword(const std::string& login, const PasswordRecord& pw);

  // Deliberately no unguarded remove/setEnabled/setAdmin: every path that can drop an admin goes through
  // removeGuarded()/update() so the last-enabled-admin invariant cannot be bypassed (REST API and imageradmin).

  /// Delete a user unless it is the last enabled admin. Atomic (one IMMEDIATE transaction).
  /// Throws AuthException(NotFound) if absent, AuthException(LastAdmin) if it would leave no enabled admin.
  void removeGuarded(const std::string& login);

  /// Apply a partial update atomically. Throws NotFound if absent, LastAdmin if the update would
  /// demote or disable the last enabled admin (nothing is changed in that case).
  void update(const std::string& login, const UserUpdate& upd);

  /// Number of users that are both admin and enabled.
  uint64_t countEnabledAdmins();

  /// Fetch a user record; returns nullopt when not found.
  std::optional<User> get(const std::string& login);

  /// Fetch stored PasswordRecord for login verification; returns nullopt when not found.
  std::optional<PasswordRecord> getPassword(const std::string& login);

  /// Return users ordered by login, with optional pagination.
  std::vector<User> list(std::optional<db::Pagination> page = std::nullopt);

  /// Return total user count.
  uint64_t count();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace auth
