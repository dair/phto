#include <metrics/Histogram.h>

namespace metrics {

Histogram::Histogram(std::string name)
  : m_name(std::move(name)) {}

void Histogram::record(std::chrono::nanoseconds duration) noexcept {
  const uint64_t ns = static_cast<uint64_t>(duration.count() < 0 ? 0 : duration.count());
  m_count.fetch_add(1, std::memory_order_relaxed);
  m_sum.fetch_add(ns, std::memory_order_relaxed);
  m_buckets[bucketIndex(ns)].fetch_add(1, std::memory_order_relaxed);
}

Histogram::Snapshot Histogram::snapshot() const {
  Snapshot s;
  s.name = m_name;
  s.count = m_count.load(std::memory_order_relaxed);
  s.sum_ns = m_sum.load(std::memory_order_relaxed);
  for (size_t i = 0; i < NUM_BUCKETS; ++i) {
    s.buckets[i] = m_buckets[i].load(std::memory_order_relaxed);
  }
  return s;
}

void Histogram::reset() noexcept {
  m_count.store(0, std::memory_order_relaxed);
  m_sum.store(0, std::memory_order_relaxed);
  for (auto& b : m_buckets) {
    b.store(0, std::memory_order_relaxed);
  }
}

size_t Histogram::bucketIndex(uint64_t ns) noexcept {
  for (size_t i = 0; i < NUM_BUCKETS - 1; ++i) {
    if (ns <= BOUNDS[i]) {
      return i;
    }
  }
  return NUM_BUCKETS - 1;
}

} // namespace metrics
