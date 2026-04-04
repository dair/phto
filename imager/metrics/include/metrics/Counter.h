#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace metrics {

/// Monotonic lock-free counter. Suitable for throughput and event counts.
class Counter {
public:
    explicit Counter(std::string name);

    Counter(const Counter&)            = delete;
    Counter& operator=(const Counter&) = delete;

    void     add(uint64_t delta) noexcept;
    void     reset() noexcept;
    uint64_t value() const noexcept;

    const std::string& name() const noexcept;

private:
    std::string           m_name;
    std::atomic<uint64_t> m_value{0};
};

} // namespace metrics
