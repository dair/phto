#include "StreamHasher.h"

#include <openssl/evp.h>

#include <stdexcept>

namespace imager {

namespace {

struct EvpMdCtxDeleter {
  void operator()(EVP_MD_CTX* c) const noexcept {
    EVP_MD_CTX_free(c);
  }
};

using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

} // namespace

struct StreamHasher::Impl {
  EvpMdCtxPtr ctx;

  Impl()
    : ctx(EVP_MD_CTX_new()) {
    if (!ctx) {
      throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
      throw std::runtime_error("EVP_DigestInit_ex failed");
    }
  }
};

StreamHasher::StreamHasher()
  : m_impl(std::make_unique<Impl>()) {}

StreamHasher::~StreamHasher() = default;

void StreamHasher::update(const uint8_t* data, size_t size) {
  if (EVP_DigestUpdate(m_impl->ctx.get(), data, size) != 1) {
    throw std::runtime_error("EVP_DigestUpdate failed");
  }
}

std::string StreamHasher::finalize() {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashLen = 0;
  if (EVP_DigestFinal_ex(m_impl->ctx.get(), hash, &hashLen) != 1) {
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
