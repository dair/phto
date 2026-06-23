#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace auth {

struct PasswordRecord {
  std::string algo; // "pbkdf2-sha256"
  uint32_t iterations{0};
  std::vector<uint8_t> salt; // 16 random bytes
  std::vector<uint8_t> hash; // 32 bytes (SHA-256 output length)
};

/// Derive a PasswordRecord from a plaintext password using PBKDF2-HMAC-SHA256.
/// Generates a fresh 16-byte random salt. @p iterations is the PBKDF2 work factor.
PasswordRecord hashPassword(std::string_view password, uint32_t iterations);

/// Constant-time verification: re-derive using the record's salt/iterations and
/// compare. Returns false (never throws) for a mismatch or an unsupported algo.
bool verifyPassword(std::string_view password, const PasswordRecord& rec) noexcept;

} // namespace auth
