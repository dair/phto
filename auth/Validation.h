#pragma once

#include <cstddef>
#include <string_view>

namespace auth {

constexpr size_t MAX_LOGIN_LEN = 64;
constexpr size_t MAX_FULLNAME_LEN = 255;
constexpr size_t MIN_PASSWORD_LEN = 8;
constexpr size_t MAX_PASSWORD_LEN = 1024;

/// Single source of truth for user-field rules (REST API and imageradmin).
/// Each returns nullptr when valid, otherwise a static human-readable message.

/// 1..64 characters from [A-Za-z0-9._@-].
const char* validateLogin(std::string_view login) noexcept;

/// 1..255 bytes.
const char* validateFullName(std::string_view fullName) noexcept;

/// 8..1024 bytes.
const char* validatePassword(std::string_view password) noexcept;

} // namespace auth
