#include "Validation.h"

#include <algorithm>

namespace auth {

namespace {

constexpr bool loginChar(char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' ||
         c == '@' || c == '-';
}

} // namespace

const char* validateLogin(std::string_view login) noexcept {
  if (login.empty() || login.size() > MAX_LOGIN_LEN || !std::ranges::all_of(login, loginChar)) {
    return "login must be 1..64 characters from [A-Za-z0-9._@-]";
  }
  return nullptr;
}

const char* validateFullName(std::string_view fullName) noexcept {
  return fullName.empty() || fullName.size() > MAX_FULLNAME_LEN ? "fullName must be 1..255 bytes" : nullptr;
}

const char* validatePassword(std::string_view password) noexcept {
  return password.size() < MIN_PASSWORD_LEN || password.size() > MAX_PASSWORD_LEN ? "password must be 8..1024 bytes"
                                                                                  : nullptr;
}

} // namespace auth
