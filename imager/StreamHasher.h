#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace imager {

/// Incremental SHA256 hasher backed by the OpenSSL EVP digest API.
/// Feed data via update(), then call finalize() to get the hex digest.
/// Not copyable; not reusable after finalize().
class StreamHasher {
public:
  StreamHasher();
  ~StreamHasher();

  StreamHasher(const StreamHasher&) = delete;
  StreamHasher& operator=(const StreamHasher&) = delete;
  StreamHasher(StreamHasher&&) = delete;
  StreamHasher& operator=(StreamHasher&&) = delete;

  /// Feed a chunk of data into the digest.
  void update(const uint8_t* data, size_t size);

  /// Finalize and return the 64-character lowercase hex SHA256 digest.
  /// Must be called exactly once; calling update() afterwards is undefined.
  std::string finalize();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace imager
