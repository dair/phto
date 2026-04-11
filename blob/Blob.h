#pragma once

#include <metrics/Metrics.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace blob {

/// Shared-ownership binary buffer.
///
/// Two-phase construction for reading from disk:
///   Blob blob(fileSize);                           // allocates writable buffer
///   stream.read(blob.writableData(), blob.size()); // fill directly
///   blob.freeze();                                 // make immutable, safe to share
///
/// Adoption construction for existing data:
///   Blob blob = Blob::fromVector(std::move(vec));  // takes ownership, already frozen
///
/// Once frozen, copying is O(1) — shared_ptr refcount bump.
/// Safe to capture by value in coroutines dispatched to a thread pool.
class Blob {
public:
  /// Construct an empty blob.
  Blob() = default;

  /// Allocate a writable buffer of the given size.
  /// Fill via writableData(), then call freeze() before sharing.
  /// Pass a non-null \p m to track blobs_alive and blob_bytes_alive gauges.
  explicit Blob(size_t size, metrics::Metrics* m = nullptr)
    : m_data(
        size > 0 ? std::shared_ptr<uint8_t[]>(
                     new uint8_t[size],
                     [m, size](uint8_t* p) noexcept {
                       delete[] p;
                       if (m) {
                         m->blobs_alive.decrement();
                         m->blob_bytes_alive.add(-static_cast<int64_t>(size));
                       }
                     }
                   )
                 : nullptr
      ),
      m_size(size) {
    if (m && size > 0) {
      m->blobs_alive.increment();
      m->blob_bytes_alive.add(static_cast<int64_t>(size));
    }
  }

  /// Adopt ownership of an existing shared_ptr (already frozen).
  Blob(std::shared_ptr<uint8_t[]> data, size_t size)
    : m_data(std::move(data)),
      m_size(size),
      m_frozen(true) {}

  /// Adopt a vector's heap allocation directly — no memcpy, no second allocation.
  static Blob fromVector(std::vector<uint8_t>&& vec, metrics::Metrics* m = nullptr) {
    const size_t n = vec.size();
    if (n == 0) {
      return {};
    }
    auto* owned = new std::vector<uint8_t>(std::move(vec));
    uint8_t* raw = owned->data();
    auto deleter = [m, n, owned](uint8_t*) noexcept {
      delete owned;
      if (m) {
        m->blobs_alive.decrement();
        m->blob_bytes_alive.add(-static_cast<int64_t>(n));
      }
    };
    if (m) {
      m->blobs_alive.increment();
      m->blob_bytes_alive.add(static_cast<int64_t>(n));
    }
    return Blob(std::shared_ptr<uint8_t[]>(raw, deleter), n);
  }

  /// Read-only pointer to the data. Valid in any state.
  const uint8_t* data() const noexcept {
    return m_data.get();
  }

  /// Writable pointer for filling the buffer.
  /// Must only be used before freeze() is called.
  uint8_t* writableData() noexcept {
    assert(!m_frozen && "writableData() called after freeze()");
    return m_data.get();
  }

  /// Mark the blob as immutable.
  /// After this call writableData() must not be used.
  /// The blob becomes safe to copy and read from multiple threads.
  void freeze() noexcept {
    m_frozen = true;
  }

  bool frozen() const noexcept {
    return m_frozen;
  }

  size_t size() const noexcept {
    return m_size;
  }

  bool empty() const noexcept {
    return m_size == 0;
  }

private:
  std::shared_ptr<uint8_t[]> m_data;
  size_t m_size{0};
  bool m_frozen{false};
};

} // namespace blob
