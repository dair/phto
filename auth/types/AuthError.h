#pragma once

#include <stdexcept>
#include <string>

namespace auth {

enum class AuthErrorCode {
  OpenFailed,
  QueryFailed,
  NotFound,
  Duplicate,
};

class AuthException: public std::runtime_error {
public:
  AuthException(AuthErrorCode code, const std::string& message)
    : std::runtime_error(message),
      m_code(code) {}

  AuthErrorCode code() const noexcept {
    return m_code;
  }

private:
  AuthErrorCode m_code;
};

} // namespace auth
