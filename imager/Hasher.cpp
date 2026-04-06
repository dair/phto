#include "Hasher.h"

#include <openssl/evp.h>

#include <memory>
#include <stdexcept>

namespace {
struct EvpMdCtxDeleter {
  void operator()(EVP_MD_CTX* c) const noexcept { EVP_MD_CTX_free(c); }
};
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;
} // namespace

namespace imager {

std::string computeSha256(const Blob& blob) {
  EvpMdCtxPtr ctx{EVP_MD_CTX_new()};
  if (!ctx) {
    throw std::runtime_error("EVP_MD_CTX_new failed");
  }

  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("EVP_DigestInit_ex failed");
  }

  // Process in 64 KB chunks
  constexpr size_t CHUNK = 64u * 1024u;
  const uint8_t* ptr = blob.data();
  size_t remaining = blob.size();
  while (remaining > 0) {
    size_t n = (remaining < CHUNK) ? remaining : CHUNK;
    if (EVP_DigestUpdate(ctx.get(), ptr, n) != 1) {
      throw std::runtime_error("EVP_DigestUpdate failed");
    }
    ptr += n;
    remaining -= n;
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashLen = 0;
  if (EVP_DigestFinal_ex(ctx.get(), hash, &hashLen) != 1) {
    throw std::runtime_error("EVP_DigestFinal_ex failed");
  }

  static constexpr char HEX[] = "0123456789abcdef";
  std::string result;
  result.reserve(hashLen * 2);
  for (unsigned int i = 0; i < hashLen; ++i) {
    result += HEX[(hash[i] >> 4) & 0x0Fu];
    result += HEX[hash[i] & 0x0Fu];
  }
  return result;
}

} // namespace imager
