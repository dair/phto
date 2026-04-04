#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace metrics {

/// Atomic gauge — current value, can go up or down.
/// Suitable for queue depth, active thread count, live blob count.
class Gauge {
public:
  explicit Gauge(std::string name);

  Gauge(const Gauge&) = delete;
  Gauge& operator=(const Gauge&) = delete;

  void increment() noexcept;        // fetch_add(1)
  void decrement() noexcept;        // fetch_sub(1)
  void add(int64_t delta) noexcept; // fetch_add(delta) — atomic, use for sized deltas
  void set(int64_t v) noexcept;
  void reset() noexcept; // store(0)
  int64_t value() const noexcept;

  const std::string& name() const noexcept;

private:
  std::string m_name;
  std::atomic<int64_t> m_value{0};
};

} // namespace metrics
