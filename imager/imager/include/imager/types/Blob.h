#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace imager {

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
    explicit Blob(size_t size)
        : m_data(size > 0 ? std::shared_ptr<uint8_t[]>(new uint8_t[size]) : nullptr)
        , m_size(size) {}

    /// Adopt ownership of an existing shared_ptr (already frozen).
    Blob(std::shared_ptr<uint8_t[]> data, size_t size)
        : m_data(std::move(data)), m_size(size), m_frozen(true) {}

    /// Adopt a vector's contents (one memcpy, then the vector is no longer needed).
    static Blob fromVector(std::vector<uint8_t>&& vec) {
        const size_t n = vec.size();
        if (n == 0) return {};
        auto* raw = new uint8_t[n];
        std::memcpy(raw, vec.data(), n);
        return Blob(std::shared_ptr<uint8_t[]>(raw), n);
    }

    /// Read-only pointer to the data. Valid in any state.
    const uint8_t* data() const noexcept { return m_data.get(); }

    /// Writable pointer for filling the buffer.
    /// Must only be used before freeze() is called.
    uint8_t* writableData() noexcept { return m_data.get(); }

    /// Mark the blob as immutable.
    /// After this call writableData() must not be used.
    /// The blob becomes safe to copy and read from multiple threads.
    void freeze() noexcept { m_frozen = true; }

    bool   frozen() const noexcept { return m_frozen; }
    size_t size()   const noexcept { return m_size; }
    bool   empty()  const noexcept { return m_size == 0; }

private:
    std::shared_ptr<uint8_t[]> m_data;
    size_t                     m_size{0};
    bool                       m_frozen{false};
};

} // namespace imager
