# Architecture Overview

This document describes the internal structure of Imager: how the components relate to each other, how the concurrency model works, and how data flows through the ingestion pipeline.

## Component Map

```
┌─────────────────────────────────────────────────────────────┐
│                     Application / CLI                        │
│         imagestore (batch)    imager_cli (interactive)       │
└──────────────────────┬──────────────────────────────────────┘
                       │ calls
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                   imager::Imager  (facade)                   │
│                                                              │
│  addFile / addImage / validateOnly / getImage / deleteImage  │
│  tagImage / untagImage / listImages / getImagesByTags        │
└────────┬──────────────┬──────────────┬──────────────────────┘
         │              │              │
         ▼              ▼              ▼
   Validators      FileStorage    MultiDatabase
   (per-format)   (multi-root)   (per-target DB)
         │              │              │
         ▼              ▼              ▼
  libjpeg/libpng  filesystem      db::Database
  libheif/LibRaw               (per target, SQLite)
  libavformat
  Pure C++ (AAE)

Shared by FileStorage and MultiDatabase:
  ┌──────────────────────────────────────────────┐
  │   coro::ThreadPool  (async fan-out executor)  │
  └──────────────────────────────────────────────┘

Used throughout:
  ┌─────────────────────────────────────────────────────────┐
  │   blob::Blob  (shared-ownership binary buffer)          │
  │   metrics::Metrics  (lock-free pipeline instrumentation) │
  │   config::AppConfig (startup TOML config)               │
  └─────────────────────────────────────────────────────────┘
```

## Libraries and Their Roles

### `config/`

Parses the TOML configuration file into `config::AppConfig`. Uses `toml++` (fetched at build time via CMake FetchContent). The parser throws `std::runtime_error` on any error. Configuration is read once at startup and never reloaded.

### `blob/`

Header-only shared-ownership binary buffer (`blob::Blob`). Provides a two-phase construction pattern: allocate a writable buffer, fill it, then call `freeze()` to make it immutable and safe to share across threads via `std::shared_ptr`. Once frozen, copying is O(1) (reference-count increment only). The blob type is also exposed as `imager::Blob` through the public API headers.

### `coro/`

Header-only C++23 coroutine primitives:

- `coro::Task<T>` — a lazy, move-only coroutine return type
- `coro::ThreadPool` — a fixed-thread pool that accepts coroutine tasks
- `coro::whenAll(...)` — fans out multiple `Task` objects concurrently and awaits all of them
- `coro::blockOn(task)` — runs a coroutine synchronously on the calling thread (bridges the async/sync boundary for the public API)

### `database/`

SQLite wrapper (`db::Database`). Manages the full schema (files, tags, associations, original names, companion records). Thread-safe via a shared mutex (readers use `std::shared_lock`, writers use `std::unique_lock`). Uses WAL journal mode and prepared statements throughout.

### `metrics/`

Lock-free instrumentation primitives: `Histogram`, `Counter`, `Gauge`, `Timer`. All recording operations are single atomic `fetch_add` calls with no allocation. The `Metrics` class is a named collection of all pipeline metrics, owned by `Imager::Impl`.

### `validations/`

One static library per format. Each implements the `validation::IValidator` interface:

```cpp
namespace validation {
    struct ValidationResult { bool valid; std::string error; };
    class IValidator {
    public:
        virtual bool supportsExtension(const std::string& ext) const = 0;
        virtual ValidationResult validate(const uint8_t* data, size_t size) const = 0;
    };
}
```

The factory in `imager/Validators.h` creates and returns all registered validators. Adding a new format requires implementing `IValidator` and registering it in the factory.

### `imager/` (the facade)

The public facade library, `libimager`. Key internal classes:

- `Hasher` — computes SHA256 via OpenSSL
- `FileStorage` — reads and writes files to/from multiple roots in parallel using coroutines
- `MultiDatabase` — fans out all writes to multiple `db::Database` instances in parallel; reads always go to `m_dbs[0]`
- `Imager::Impl` — the private implementation holding the thread pool, storage, database fan-out, validators, and metrics

### `imagestore/`

The batch import CLI. Reads paths from stdin, dispatches each to `libimager` with bounded concurrency (via `std::counting_semaphore`), and reports progress. Composed of `ProgressReporter`, `ErrorFile`, `Stats`, and display-mode helpers.

---

## Data Flow: Ingesting a File

### `addFile(path)` Pipeline

```
1.  Read file from disk into Blob          (coro, thread pool, metrics: stage_read)
    ↓
2.  Validate format                         (coro, validator for extension, metrics: validate)
    ↑ concurrent with step 3
3.  Compute SHA256 hash                     (coro, Hasher, metrics: hash)
    ↓ (join after both complete)
4.  Acquire write mutex                     (metrics: mutex_wait)
    ↓
5.  Check for duplicate in database         (db::Database read, metrics: dedup_check)
    → if duplicate: release mutex, return DuplicateFile
    ↓
6.  Write file to all storage roots         (coro, FileStorage, fan-out across roots, metrics: storage_write)
    ↓
7.  Insert record into all databases        (coro, MultiDatabase, fan-out, metrics: db_insert)
    ↓
8.  Release write mutex
    ↓
9.  Return AddResult{Ok, sha256_id}
```

Steps 2 and 3 (validate + hash) run **concurrently** — two coroutines are launched with `coro::whenAll`. Steps 6 and 7 (storage write + DB insert) fan out across all configured targets in parallel.

### `addImage(blob, filename)` Pipeline

Identical to `addFile` but step 1 (disk read) is skipped — the caller provides the `Blob` directly.

### `validateOnly(blob, filename)` Pipeline

Steps 2–5 only. Steps 6 and 7 (write and insert) are skipped. Used by `imagestore --dry-run`.

---

## Concurrency Model

### Internal Thread Pool

`Imager::Impl` owns one `coro::ThreadPool` shared by `FileStorage` and `MultiDatabase`. The pool size is not currently configurable; it is sized internally.

Coroutines schedule work onto the thread pool using `co_await pool.schedule()`. This means the thread pool is the executor for all asynchronous work inside the library.

### Write Serialization

A single `std::mutex` (`writeMutex`) serializes the dedup check and write across all threads. This guarantees that:

1. No two concurrent `addImage` calls can both pass the dedup check for the same file.
2. The dedup check and write are atomic from the perspective of concurrent callers.

This is a deliberate design choice: the mutex is held only during the dedup check and the write operations themselves, not during validation and hashing (which run before the mutex is acquired). For a typical mix of file sizes, lock contention is low.

### Coroutine Fan-Out

Storage writes and database inserts use `coro::whenAll` to run all N targets in parallel:

```cpp
// Conceptual sketch of what MultiDatabase does
std::vector<coro::Task<void>> tasks;
for (auto& db : m_dbs) {
    tasks.push_back(db.insertFile(id, name, size, ext));
}
co_await coro::whenAll(std::move(tasks));
```

A failure in any task causes the whole `whenAll` to fail, which triggers the rollback path.

### Public API is Synchronous

All `imager::Imager` methods are synchronous from the caller's perspective. Internally they use `coro::blockOn()` to run coroutines to completion on the calling thread. Callers do not need to know about coroutines.

---

## Multi-Root Storage Model

Each target in the configuration corresponds to one `db::Database` instance (managed by `MultiDatabase`) and one storage root path (managed by `FileStorage`).

All writes happen in parallel to all targets. All reads go to the first target (`m_dbs[0]` / the first root). If the first root is unavailable for a read, `FileStorage` retries the remaining roots in order.

The invariant is: **all targets contain exactly the same set of files and database records**. This is enforced by the all-or-nothing write protocol.

---

## Error Handling and Rollback

### Storage Write Failure

If a file write fails on any root after succeeding on others, `FileStorage` deletes the successfully-written copies. The `addImage` call returns `StorageError`.

### Database Insert Failure

If a database insert fails on any database after succeeding on others, `MultiDatabase` deletes the records from the successful databases. If the storage write has already completed, the file is also removed from all storage roots. The `addImage` call returns `DatabaseError`.

### Partial Rollback Limitations

Rollback is best-effort in the sense that the compensation operations (deleting written files, removing inserted records) are attempted but not themselves retried on failure. In practice, a rollback failure would only occur if the same storage or database error that caused the original failure also affects the cleanup — an unusual scenario.

---

## AAE Sidecar Integration

AAE sidecar handling is integrated into `Imager::Impl`. The sidecar detection, parent lookup, orphan storage, relocation logic, and cascade delete are all implemented in `Imager.cpp` using the `original_name` and `file_companion` tables in `MultiDatabase`. See [Storage and Data Model](storage.md#aae-sidecars) for a complete description.

---

## Build Structure

The project uses CMake with a top-level `CMakeLists.txt` that adds each subdirectory as a subproject. The build system is fully modular: each library compiles independently.

```
CMakeLists.txt
  ├── metrics/          → libmetrics
  ├── blob/             → libbloblib (header-only, compiled at link time)
  ├── coro/             → libcoro (header-only)
  ├── database/         → libdatabase
  ├── config/           → libconfig
  ├── validations/
  │   ├── jpeg/         → libjpeg_validator
  │   ├── png/          → libpng_validator
  │   ├── heic/         → libheic_validator
  │   ├── nef/          → libnef_validator
  │   ├── mov/          → libmov_validator
  │   └── aae/          → libaae_validator
  ├── imager/           → libimager  (links all of the above)
  └── imagestore/       → imagestore (executable, links libimager)
```

All artifacts go to `/tmp/imager-build` via the default CMake preset.
