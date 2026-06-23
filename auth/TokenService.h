#pragma once

#include <auth/types/User.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace auth {

class TokenService {
public:
  TokenService(std::string secret, std::string issuer, uint32_t ttlSeconds);

  /// Issue a signed HS256 JWT for the given user.
  std::string issue(const User& user);

  struct Claims {
    std::string login;
    std::string fullName;
    bool isAdmin{false};
  };

  /// Verify a Bearer token: check signature, expiry, and issuer.
  /// Returns nullopt on any failure (never throws).
  std::optional<Claims> verify(std::string_view bearerToken);

private:
  std::string m_secret;
  std::string m_issuer;
  uint32_t m_ttlSeconds;
};

} // namespace auth
