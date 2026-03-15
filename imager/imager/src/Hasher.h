#pragma once

#include <cstdint>
#include <string>

namespace imager {

/// Compute the SHA256 hash of data and return it as a 64-character lowercase hex string.
/// Throws std::runtime_error on OpenSSL failure.
std::string computeSha256(const uint8_t* data, size_t size);

} // namespace imager
