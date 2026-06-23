#include "TokenService.h"

#include <openssl/rand.h>

#include <chrono>
#include <iomanip>
#include <jwt/jwt.hpp>
#include <sstream>
#include <stdexcept>

namespace {

std::string randomHex(std::size_t bytes) {
  std::vector<unsigned char> buf(bytes);
  if (RAND_bytes(buf.data(), static_cast<int>(bytes)) != 1) {
    throw std::runtime_error("RAND_bytes failed");
  }
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (auto b : buf) {
    oss << std::setw(2) << static_cast<unsigned>(b);
  }
  return oss.str();
}

} // namespace

namespace auth {

TokenService::TokenService(std::string secret, std::string issuer, uint32_t ttlSeconds)
  : m_secret{std::move(secret)},
    m_issuer{std::move(issuer)},
    m_ttlSeconds{ttlSeconds} {}

std::string TokenService::issue(const User& user) {
  using namespace jwt::params;
  using clock = std::chrono::system_clock;

  const auto now = clock::now();
  const auto exp = now + std::chrono::seconds{m_ttlSeconds};

  jwt::jwt_object obj{algorithm("HS256"), secret(m_secret), payload({{"iss", m_issuer}})};
  obj.add_claim("sub", user.login)
    .add_claim("name", user.fullName)
    .add_claim("role", user.isAdmin ? std::string{"admin"} : std::string{"user"})
    .add_claim("iat", now)
    .add_claim("exp", exp)
    .add_claim("jti", randomHex(16));

  return obj.signature();
}

std::optional<TokenService::Claims> TokenService::verify(std::string_view bearerToken) {
  namespace p = jwt::params;

  try {
    auto obj = jwt::decode(
      std::string{bearerToken}, p::algorithms({"HS256"}), p::secret(m_secret), p::issuer(m_issuer), p::verify(true)
    );

    const auto& pl = obj.payload();
    Claims c;
    c.login = pl.get_claim_value<std::string>("sub");
    c.fullName = pl.get_claim_value<std::string>("name");
    const auto role = pl.get_claim_value<std::string>("role");
    c.isAdmin = (role == "admin");
    return c;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace auth
