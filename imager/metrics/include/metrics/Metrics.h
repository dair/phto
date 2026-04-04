#pragma once

#include "Counter.h"
#include "Gauge.h"
#include "Histogram.h"
#include "Snapshot.h"
#include "Timer.h"

namespace metrics {

/// Singleton registry of all named metrics.
///
/// All members are public for direct access: metrics::Metrics::get().hash.record(...)
///
/// Thread-safe: all primitives are lock-free atomics.
/// Meyer's singleton: thread-safe construction guaranteed by C++11+.
class Metrics {
public:
  static Metrics& get();

  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  // --- Per-image pipeline ---
  Histogram addimage_total{"addimage_total"};
  Histogram validate{"validate"};
  Histogram hash{"hash"};
  Histogram dedup_check{"dedup_check"};
  Histogram storage_write{"storage_write"};
  Histogram storage_write_root{"storage_write_root"};
  Histogram db_insert{"db_insert"};
  Histogram db_insert_single{"db_insert_single"};
  Histogram mutex_wait{"mutex_wait"};

  // --- Thread pool ---
  Histogram pool_schedule_latency{"pool_schedule_latency"};
  Gauge pool_queue_depth{"pool_queue_depth"};
  Gauge pool_active_threads{"pool_active_threads"};

  // --- Storage I/O ---
  Counter storage_bytes_written{"storage_bytes_written"};
  Counter storage_bytes_read{"storage_bytes_read"};
  Histogram storage_read_duration{"storage_read_duration"};

  // --- Database (populated when Database.cpp is instrumented) ---
  Histogram db_read_duration{"db_read_duration"};
  Histogram db_write_duration{"db_write_duration"};

  // --- Memory ---
  Gauge blobs_alive{"blobs_alive"};
  Gauge blob_bytes_alive{"blob_bytes_alive"};

  // --- Throughput ---
  Counter images_added{"images_added"};
  Counter images_failed{"images_failed"};

  /// Take a consistent point-in-time snapshot of all metrics.
  FullSnapshot snapshot() const;

  /// Reset all counters, histograms, and gauges to zero.
  /// Useful for periodic reporting windows.
  void reset();

private:
  Metrics() = default;
};

} // namespace metrics
