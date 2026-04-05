#pragma once

#include "imager/types/Blob.h"

namespace imager {

/// Compute the SHA256 hash of blob's data.
/// Returns a 64-character lowercase hex string.
/// Throws std::runtime_error on OpenSSL failure.
std::string computeSha256(const Blob& blob);

} // namespace imager
