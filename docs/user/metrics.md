# Metrics and Monitoring

Imager has always-on, lock-free instrumentation built into the ingestion pipeline. Metrics help you understand throughput, identify bottlenecks, and observe the health of a running import. This document describes the available metrics, how to access them, and how to interpret the output.

## Overview

The `metrics::Metrics` class is owned by each `Imager` instance and tracks all pipeline activity since the instance was created (or since the last `reset()` call). All metric primitives are lock-free atomics — recording a metric is a single `fetch_add` with no heap allocation and no mutex.

## Accessing Metrics

```cpp
const metrics::Metrics& m = lib.metrics();
```

The reference is valid for the lifetime of the `Imager` object. You can access individual metrics directly:

```cpp
// Read a counter
uint64_t added = m.images_added.value();

// Take a full point-in-time snapshot of everything
metrics::FullSnapshot snap = m.snapshot();
```

### Formatted Output

The `metrics::format()` function renders a full snapshot as a human-readable string:

```cpp
#include <metrics/Snapshot.h>
std::cerr << metrics::format(lib.metrics().snapshot());
```

When using `imager_cli` with `--metrics`, this output is printed to stderr automatically.

### Resetting

```cpp
lib.metrics().reset();  // Resets all counters, histograms, and gauges to zero
```

This is useful if you want periodic reporting windows rather than cumulative totals.

---

## Metric Types

### Counter

A monotonically increasing 64-bit integer. Records cumulative totals.

```cpp
uint64_t v = m.images_added.value();
m.images_added.increment();      // +1
m.images_added.add(n);           // +n
```

### Gauge

A signed 64-bit integer that can go up and down. Used for in-flight counts.

```cpp
int64_t v = m.inflight_validating.value();
m.inflight_validating.increment();
m.inflight_validating.decrement();
m.inflight_validating.add(-n);
```

### Histogram

A fixed-bucket latency histogram covering 100 ns to 30 s on a logarithmic scale. Recording is one atomic `fetch_add`.

```cpp
m.validate.record(std::chrono::nanoseconds(3'200'000));  // 3.2ms
```

A snapshot captures count, sum, and 20 bucket counts. From a snapshot you can compute mean latency and percentiles.

---

## Available Metrics

### Pipeline Stage Timings (Histograms)

These measure end-to-end latency of key operations per file ingested.

| Metric | Description |
|--------|-------------|
| `addimage_total` | Total wall time for one `addImage` / `addFile` call |
| `validate` | Time to validate a file (format-specific decoder) |
| `hash` | Time to compute the SHA256 hash |
| `dedup_check` | Time to check whether the hash already exists in the database |
| `storage_write` | Total time to write the file to all storage roots (parallel) |
| `storage_write_root` | Per-root write time (recorded once per target per file) |
| `db_insert` | Total time to insert records into all databases (parallel) |
| `db_insert_single` | Per-database insert time |
| `mutex_wait` | Time spent waiting for the write mutex |
| `file_read` | Time to read the file from disk (only for `addFile`) |

**Interpreting these:** If `storage_write` is large relative to `addimage_total`, storage I/O is the bottleneck. If `mutex_wait` is large, concurrent goroutines are queuing up and the ingestion throughput may be serialization-bound.

### Storage I/O (Counters + Histogram)

| Metric | Description |
|--------|-------------|
| `storage_bytes_written` | Cumulative bytes written to storage across all roots |
| `storage_bytes_read` | Cumulative bytes read from storage (for `getImageData`) |
| `storage_read_duration` | Histogram of read latencies |

### Throughput (Counters)

| Metric | Description |
|--------|-------------|
| `images_added` | Number of files successfully stored |
| `images_failed` | Number of files that failed ingestion for any reason |

### Pipeline Stage Progress (Counters)

These counters track how many files and bytes have completed each stage of the pipeline. They are the data source for the `imagestore --graph` display.

| Metric | Description |
|--------|-------------|
| `stage_read` | Files read from disk (only for `addFile`) |
| `stage_read_bytes` | Bytes read from disk |
| `stage_validated` | Files that completed validation |
| `stage_validated_bytes` | Bytes through validation |
| `stage_hashed` | Files that completed hashing |
| `stage_hashed_bytes` | Bytes through hashing |
| `stage_dedup_checked` | Files that completed the dedup check |
| `stage_stored` | Files successfully written to storage |
| `stage_db_inserted` | Files successfully inserted into the database |
| `stage_db_inserted_bytes` | Bytes through the database insert stage |

### In-Flight Gauges (Files)

Gauges that show how many files are currently active at each pipeline stage. Useful for understanding concurrency utilization.

| Metric | Description |
|--------|-------------|
| `inflight_reading` | Files being read from disk (`addFile` only) |
| `inflight_validating` | Files currently being validated |
| `inflight_hashing` | Files currently being hashed |
| `inflight_waiting_mutex` | Files waiting to acquire the write mutex |
| `inflight_dedup_checking` | Files in the dedup check |
| `inflight_writing_storage` | Files being written to storage |
| `inflight_inserting_db` | Files being inserted into the database |

### In-Flight Gauges (Bytes)

The same set of gauges but tracking bytes instead of file counts.

`inflight_reading_bytes`, `inflight_validating_bytes`, `inflight_hashing_bytes`, `inflight_waiting_mutex_bytes`, `inflight_dedup_checking_bytes`, `inflight_writing_storage_bytes`, `inflight_inserting_db_bytes`.

### Thread Pool Metrics

| Metric | Description |
|--------|-------------|
| `pool_schedule_latency` | Histogram of task scheduling latency |
| `pool_queue_depth` | Current number of tasks waiting in the pool queue |
| `pool_active_threads` | Current number of threads executing tasks |

### Memory Metrics

| Metric | Description |
|--------|-------------|
| `blobs_alive` | Number of `Blob` objects currently live |
| `blob_bytes_alive` | Total bytes held by live `Blob` objects |

These track memory pressure from in-flight file buffers.

---

## Interpreting the Snapshot Format

A formatted snapshot output looks like:

```
=== Pipeline Timings (latency per file) ===

addimage_total   count=1234   avg=15.3ms   p50=14ms   p95=28ms   p99=45ms
validate         count=1234   avg=3.8ms    p50=3ms    p95=7ms    p99=12ms
hash             count=1234   avg=1.4ms    p50=1ms    p95=3ms    p99=5ms
dedup_check      count=1234   avg=0.4ms    p50=<1ms   p95=1ms    p99=2ms
storage_write    count=1234   avg=6.2ms    p50=5ms    p95=14ms   p99=22ms
db_insert        count=1234   avg=2.8ms    p50=2ms    p95=6ms    p99=9ms
mutex_wait       count=1234   avg=0.8ms    p50=<1ms   p95=3ms    p99=7ms
file_read        count=1234   avg=2.1ms    p50=2ms    p95=4ms    p99=7ms

=== Throughput ===

images_added    12340
images_failed   2
storage_bytes_written   45,321,891,840 bytes (42.2 GB)

=== Pipeline Progress ===

stage            files       bytes
read             12342       45,344,301,056
validated        12342       45,344,301,056
hashed           12342       45,344,301,056
dedup_checked    12342       45,344,301,056
stored           12340       45,322,100,736
db_inserted      12340       45,322,100,736
```

---

## Using Metrics for Bottleneck Analysis

### Finding the Slowest Stage

Compare the average latency of each histogram:

- `validate` slow → heavy format decoder (large MOV/MP4 trial decode, large JPEG decode)
- `hash` slow → large files; consider a faster machine with better SHA256 support (AES-NI helps OpenSSL)
- `storage_write` slow → disk I/O bottleneck; reduce `--jobs` if spinning HDD
- `db_insert` slow → SQLite bottleneck; check if the database file is on a slow device
- `mutex_wait` high → high concurrency; too many threads competing for the write slot

### Checking Parallelism Utilization

Compare in-flight gauges against your `--jobs` value. If `inflight_validating` is always 1, validation is not parallelizing with storage. If `pool_queue_depth` is persistently nonzero, the thread pool is the constraint.

### Monitoring a Live Import

For a running `imagestore` session, the `--graph` flag displays a live view of in-flight gauges. For embedding scenarios, poll `lib.metrics().snapshot()` at regular intervals and inspect the counters to compute files-per-second and MB/s.

```cpp
// Measure throughput over a 5-second window
auto snap1 = lib.metrics().snapshot();
std::this_thread::sleep_for(std::chrono::seconds(5));
auto snap2 = lib.metrics().snapshot();

double filesPerSec = (snap2.images_added - snap1.images_added) / 5.0;
double mbPerSec    = (snap2.storage_bytes_written - snap1.storage_bytes_written)
                     / 5.0 / 1024.0 / 1024.0;
```
