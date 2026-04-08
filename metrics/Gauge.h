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


/// RAII guard: increments a Gauge on construction, decrements on destruction.
/// Ensures correct decrement even when exceptions unwind the stack.
struct GaugeGuard {
  Gauge& gauge;
  explicit GaugeGuard(Gauge& g) noexcept : gauge(g) { gauge.increment(); }
  ~GaugeGuard() { gauge.decrement(); }

  GaugeGuard(const GaugeGuard&) = delete;
  GaugeGuard& operator=(const GaugeGuard&) = delete;
};

/// RAII guard for a paired file-count + byte-count gauge.
/// Increments both on construction, decrements both on destruction.
struct SizedGaugeGuard {
  Gauge& files;
  Gauge& bytes;
  int64_t size;
  SizedGaugeGuard(Gauge& f, Gauge& b, int64_t s) noexcept
    : files(f), bytes(b), size(s) { files.increment(); bytes.add(size); }
  ~SizedGaugeGuard() { files.decrement(); bytes.add(-size); }

  SizedGaugeGuard(const SizedGaugeGuard&) = delete;
  SizedGaugeGuard& operator=(const SizedGaugeGuard&) = delete;
};

} // namespace metrics
