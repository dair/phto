#include "PasswordHash.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdexcept>

namespace auth {

namespace {

constexpr int SALT_LEN = 16;
constexpr int HASH_LEN = 32;
constexpr std::string_view ALGO = "pbkdf2-sha256";

} // namespace

PasswordRecord hashPassword(std::string_view password, uint32_t iterations) {
  PasswordRecord rec;
  rec.algo = std::string(ALGO);
  rec.iterations = iterations;
  rec.salt.resize(SALT_LEN);
  rec.hash.resize(HASH_LEN);

  if (RAND_bytes(rec.salt.data(), SALT_LEN) != 1) {
    throw std::runtime_error("auth::hashPassword: RAND_bytes failed");
  }

  if (PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        rec.salt.data(),
        SALT_LEN,
        static_cast<int>(iterations),
        EVP_sha256(),
        HASH_LEN,
        rec.hash.data()
      ) != 1) {
    throw std::runtime_error("auth::hashPassword: PKCS5_PBKDF2_HMAC failed");
  }

  return rec;
}

bool verifyPassword(std::string_view password, const PasswordRecord& rec) noexcept {
  if (rec.algo != ALGO) {
    return false;
  }

  const int hashLen = static_cast<int>(rec.hash.size());
  std::vector<uint8_t> derived(static_cast<size_t>(hashLen));

  if (PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        rec.salt.data(),
        static_cast<int>(rec.salt.size()),
        static_cast<int>(rec.iterations),
        EVP_sha256(),
        hashLen,
        derived.data()
      ) != 1) {
    return false;
  }

  return CRYPTO_memcmp(derived.data(), rec.hash.data(), static_cast<size_t>(hashLen)) == 0;
}

} // namespace auth
