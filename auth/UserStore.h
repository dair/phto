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

  /// Delete a user by login. Throws AuthException(NotFound) if absent.
  void remove(const std::string& login);

  /// Replace stored password. Throws AuthException(NotFound) if login absent.
  void setPassword(const std::string& login, const PasswordRecord& pw);

  /// Toggle enabled flag. Throws AuthException(NotFound) if login absent.
  void setEnabled(const std::string& login, bool enabled);

  /// Toggle admin flag. Throws AuthException(NotFound) if login absent.
  void setAdmin(const std::string& login, bool isAdmin);

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
