#pragma once

#include "Counter.h"
#include "Gauge.h"
#include "Histogram.h"
#include "Snapshot.h"
#include "Timer.h"

namespace metrics {

/// Registry of all named metrics.
///
/// All members are public for direct access: metrics.hash.record(...)
///
/// Thread-safe: all primitives are lock-free atomics.
/// Owned by Imager::Impl and injected by reference into FileStorage and MultiDatabase.
class Metrics {
public:
  Metrics() = default;

  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;
  Metrics(Metrics&&) = delete;
  Metrics& operator=(Metrics&&) = delete;

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

  // --- Progress: cumulative stage counters ---
  Counter stage_read{"stage_read"};                             // files read from disk (addFile only)
  Counter stage_read_bytes{"stage_read_bytes"};                 // bytes read from disk (addFile only)
  Counter stage_validated{"stage_validated"};                   // files that completed validation
  Counter stage_validated_bytes{"stage_validated_bytes"};       // bytes that completed validation
  Counter stage_hashed{"stage_hashed"};                         // files that completed hashing
  Counter stage_hashed_bytes{"stage_hashed_bytes"};             // bytes that completed hashing
  Counter stage_dedup_checked{"stage_dedup_checked"};           // files that completed dedup check
  Counter stage_stored{"stage_stored"};                         // files that completed storage write
  // stage_stored_bytes: reuse existing storage_bytes_written
  Counter stage_db_inserted{"stage_db_inserted"};               // files that completed DB insert
  Counter stage_db_inserted_bytes{"stage_db_inserted_bytes"};   // bytes that completed DB insert

  // --- Progress: in-flight gauges (files) ---
  Gauge inflight_reading{"inflight_reading"};                   // files being read from disk (addFile only)
  Gauge inflight_validating{"inflight_validating"};             // files being validated
  Gauge inflight_hashing{"inflight_hashing"};                   // files being hashed
  Gauge inflight_waiting_mutex{"inflight_waiting_mutex"};       // files waiting for write mutex
  Gauge inflight_dedup_checking{"inflight_dedup_checking"};     // files in dedup check
  Gauge inflight_writing_storage{"inflight_writing_storage"};   // files being written to storage
  Gauge inflight_inserting_db{"inflight_inserting_db"};         // files being inserted into DB

  // --- Progress: in-flight gauges (bytes) ---
  Gauge inflight_reading_bytes{"inflight_reading_bytes"};
  Gauge inflight_validating_bytes{"inflight_validating_bytes"};
  Gauge inflight_hashing_bytes{"inflight_hashing_bytes"};
  Gauge inflight_waiting_mutex_bytes{"inflight_waiting_mutex_bytes"};
  Gauge inflight_dedup_checking_bytes{"inflight_dedup_checking_bytes"};
  Gauge inflight_writing_storage_bytes{"inflight_writing_storage_bytes"};
  Gauge inflight_inserting_db_bytes{"inflight_inserting_db_bytes"};

  // --- Progress: file read latency (addFile only) ---
  Histogram file_read{"file_read"};

  /// Take a consistent point-in-time snapshot of all metrics.
  FullSnapshot snapshot() const;

  /// Reset all counters, histograms, and gauges to zero.
  /// Useful for periodic reporting windows.
  void reset();
};

} // namespace metrics
