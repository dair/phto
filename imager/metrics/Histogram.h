#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace metrics {

/// Fixed-bucket, lock-free latency histogram.
///
/// Buckets cover 100ns to 30s on a log scale.
/// Recording is a single atomic fetch_add — no locks, no allocation.
class Histogram {
public:
  // Bucket upper bounds in nanoseconds (last bucket is +Inf)
  static constexpr size_t NUM_BUCKETS = 20;
  static constexpr uint64_t BOUNDS[NUM_BUCKETS - 1] = {
    100,            // 100 ns
    500,            // 500 ns
    1'000,          //   1 us
    5'000,          //   5 us
    10'000,         //  10 us
    50'000,         //  50 us
    100'000,        // 100 us
    500'000,        // 500 us
    1'000'000,      //   1 ms
    5'000'000,      //   5 ms
    10'000'000,     //  10 ms
    50'000'000,     //  50 ms
    100'000'000,    // 100 ms
    500'000'000,    // 500 ms
    1'000'000'000,  //   1 s
    2'000'000'000,  //   2 s
    5'000'000'000,  //   5 s
    10'000'000'000, //  10 s
    30'000'000'000, //  30 s
                    // +Inf (bucket 19)
  };

  struct Snapshot {
    std::string name;
    uint64_t count{0};
    uint64_t sum_ns{0};
    uint64_t buckets[NUM_BUCKETS]{};
  };

  explicit Histogram(std::string name);

  Histogram(const Histogram&) = delete;
  Histogram& operator=(const Histogram&) = delete;

  /// Record a duration sample. O(1), lock-free.
  void record(std::chrono::nanoseconds duration) noexcept;

  /// Take a consistent point-in-time snapshot.
  Snapshot snapshot() const;

  /// Reset all counts to zero.
  void reset() noexcept;

private:
  static size_t bucketIndex(uint64_t ns) noexcept;

  std::string m_name;
  std::atomic<uint64_t> m_count{0};
  std::atomic<uint64_t> m_sum{0};
  std::array<std::atomic<uint64_t>, NUM_BUCKETS> m_buckets{};
};

} // namespace metrics
