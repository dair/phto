# Work Log

## [2026-06-23 10:00] - Config: ServerConfig/AuthConfig structs + TOML parse (checkpoint A2)

- **Agent**: cpp-spec-coder
- **Task**: Implement milestone M-A checkpoint A2 from 0022.SERVER.md — extend `config` library with `ServerConfig` and `AuthConfig` structs, TOML parsing for optional `[server]` and `[auth]` sections, validation, and CPPUnit tests.
- **Outcome**: Added `ServerConfig` and `AuthConfig` structs to `config/Config.h` (namespace `config`); added `server` and `auth` members to `AppConfig`. Extended `loadConfig` in `Config.cpp` to parse both optional sections using existing toml++ patterns — absent sections keep struct defaults. Validation throws `std::runtime_error` for: `port` out of 1..65535, `max_upload_mb <= 0`, `token_ttl_seconds <= 0`, `pbkdf2_iterations < 1000`, and type mismatches. Added 4 new CPPUnit fixture classes (20 total test cases) to `config/test/ConfigTest.cpp`: backward-compat defaults, fully-populated sections, partial sections, and invalid-value error cases. No new files, no CMakeLists changes. Full build clean; 16/16 test suites pass (100%).
- **Next Step**: Checkpoint A3 (next M-A milestone per 0022.SERVER.md).

## [2026-04-13 09:00] - Large file streaming support (spec 0017)

- **Agent**: cpp-spec-coder
- **Task**: Implement 0017.LARGE_FILES.md — streaming pipeline for files > 256 MB and per-format size limits.
- **Outcome**: Added `ErrorCode::TooLarge`. Added `fileSizeLimits` to `AppConfig` with TOML `[file_size_limits]` parsing (human-readable strings: KB/MB/GB/TB) and compiled defaults. Added `IStreamValidator` abstract interface to `ImageValidator.h`. Created `StreamHasher` (OpenSSL EVP incremental SHA256). Implemented 6 stream validators (each in a separate TU to prevent enum collisions): JPEG (`jpeg_stdio_src`), PNG (file-backed `pngReadCallback`), HEIC (`heif_reader` callbacks), MOV (file-backed `AVIOContext`), NEF (`libraw_open_file`), AAE (first 64 KB read). Added `writeFileFromDiskAsync`/`writeToRootFromDisk` to `FileStorage` for 4 MB chunk streaming writes with rollback. Updated `Imager::addFile` to: (1) stat file, (2) reject with `TooLarge` if over format limit, (3) route to `addFileLarge` if > 256 MB, (4) else existing Blob path unchanged. Implemented `addFileLarge`: parallel hash+validate coroutines (Pass 1), then streaming write (Pass 2). Added `validateOnlyFile(path)` for dry-run with large files. Added `createDefaultStreamValidators()` factory. Updated `CMakeLists.txt` and fixed `TooLarge` case in sample CLI. Added integration tests (`SizeLimitTest`, `StreamingPathTest`) in `LargeFileTest.cpp`. Build clean, 14/14 tests pass.
- **Next Step**: None specified.

## [2026-04-12 10:00] - Verbose/Normal output redesign (spec 0016)

- **Agent**: cpp-spec-coder
- **Task**: Implement 0016.VERBOSE_OUTPUT.md: remove --graph mode, add Verbose display mode with real-time per-slot ANSI rendering, make Normal mode silent, add StageCallback to Imager::addFile.
- **Outcome**: `imager/Types.h` — added `ProcessingStage` enum + `StageCallback` typedef. `imager/Imager.h` — updated `addFile` signature with optional `StageCallback onStage=nullptr`. `imager/Imager.cpp` — refactored `addImage` body into `Impl::addImageImpl(blob, filename, onStage)` with callback invocations at each gauge-guard stage; `addImage` delegates with nullptr; `addFile` invokes `onStage(Reading)` before file read. `imagestore/DisplayMode.h` — replaced `Graph` with `Verbose`. New files: `SlotTracker.h/.cpp` (mutex-protected per-slot stage tracker), `ResultLog.h/.cpp` (thread-safe result appender with TTY/non-TTY ANSI scrolling-region support). `ProgressReporter.h/.cpp` — stripped graph members/rendering, added verbose TTY setup (ANSI scrolling region, cursor hide, 200ms render loop calling `renderVerbose()`), Normal mode now fully silent. `main.cpp` — removed --graph flag, added -v/-q mutual exclusion, wires SlotTracker+ResultLog into ProgressReporter, worker lambda acquires/releases slot with RAII, passes onStage callback to addFile. `CMakeLists.txt` — added ResultLog.cpp + SlotTracker.cpp. Build clean, all 12 tests pass.
- **Next Step**: Task #4 — test verbose and normal output modes.

## [2026-04-11 12:00] - Docs fix, AmbiguousSidecar error code, sidecar rollback doc, whenAll invariant, move createDefaultValidators (3.7/A7-A9, 3.8/C8-C11)

- **Agent**: cpp-spec-coder
- **Task**: Five independent changes: (A7-A9) fix README.md doc inaccuracies; (C8) add AmbiguousSidecar ErrorCode and use it; (C9) document sidecar rollback cascade behaviour; (C10) expand whenAll scheduling invariant comment; (C11) move createDefaultValidators() from header to Validators.cpp.
- **Outcome**: `docs/plan/README.md` corrected (SHA256-only identity, validations/ plural, added HEIC/NEF/MOV/AAE/imagestore/sidecar mentions). `ErrorCode.h` gains `AmbiguousSidecar`. `Imager.cpp` uses `AmbiguousSidecar` for ambiguous-sidecar return and documents that `deleteFile` cascades to `original_name` via FK. `WhenAll.h` comment expanded with UB consequence and caller guidance. `Validators.cpp` created with function body; header retains only declaration; `CMakeLists.txt` updated; `sample/main.cpp` switch extended to cover new error code. Build clean, all 11 tests pass.
- **Next Step**: None specified.

## [2026-04-11 11:30] - Fix fromVector() double-copy (3.4) and enforce freeze() (3.5)

- **Agent**: cpp-spec-coder
- **Task**: Two improvements to `blob/Blob.h`: (1) eliminate the memcpy+extra-allocation in `fromVector()` by adopting the vector's heap storage via a custom deleter; (2) add an `assert(!m_frozen)` guard in `writableData()` to catch post-freeze writes.
- **Outcome**: Replaced `fromVector()` implementation to move the vector onto the heap and wrap its `data()` pointer in a `shared_ptr` with a deleter that `delete`s the owned vector; removed `#include <cstring>`, added `#include <cassert>`; added `assert(!m_frozen && "writableData() called after freeze()")` in `writableData()`. Build clean, all 11 test suites pass.
- **Next Step**: None specified.

## [2026-04-11 10:00] - Wire DB timing metrics (2.1 / C4)

- **Agent**: cpp-spec-coder
- **Task**: Add optional `metrics::Metrics*` to `Database` constructor; record `db_read_duration`/`db_write_duration` in all read/write methods; update `MultiDatabase.cpp` to pass `&m_metrics` to each `Database`.
- **Outcome**: Forward-declared `metrics::Metrics` in `Database.h`; added `metrics::Metrics* = nullptr` ctor param; stored pointer in `Impl`; added `readTimer()`/`writeTimer()` helpers returning `std::optional<metrics::Timer>`; instrumented all write/read methods; linked `database` against `metrics_lib`; `MultiDatabase` passes `&m_metrics` to each `Database` ctor. Build clean.
- **Next Step**: Metrics layer tests (task #10 / B4, B9).

## [2026-04-08 14:00] - Wire blob lifetime metrics (C5) and thread pool metrics (C6)

- **Agent**: cpp-spec-coder
- **Task**: Wire `blobs_alive`/`blob_bytes_alive` gauges into `blob::Blob` and `pool_queue_depth`/`pool_active_threads`/`pool_schedule_latency` into `coro::ThreadPool`; connect both to the `Metrics` instance in `Imager::Impl`.
- **Outcome**: `Blob(size_t, metrics::Metrics* = nullptr)` — custom deleter decrements gauges on free; constructor increments them. `Blob::fromVector` simplified (dropped double-copy via temporary `raw` buffer). `ThreadPool(size_t, metrics::Metrics* = nullptr)` — added `QueueEntry{handle, enqueued}`, `enqueue` increments `pool_queue_depth`, `workerLoop` decrements depth and records `pool_schedule_latency` + `pool_active_threads`. `Imager.cpp`: pool constructed with `&metrics`; `Blob(fileSize, &m_impl->metrics)` in `addFile`. All existing call sites unaffected (default `nullptr`). clang-format pending (Bash unavailable).
- **Next Step**: Run `clang-format` on the three files, then build and ctest.

## [2026-04-11 10:30] - clang-format + build verification (C5, C6)

- **Agent**: cpp-spec-coder
- **Task**: Apply `clang-format` to `blob/Blob.h` and `coro/ThreadPool.h`; verify build.
- **Outcome**: `clang-format -i` applied; full CMake build clean (all targets built, no warnings). C5 and C6 fully complete.
- **Next Step**: Await next task assignment.

## [2026-04-08 08:30] - Config semantic validation (2.4 / C7)

- **Agent**: cpp-spec-coder
- **Task**: Reject configs with duplicate root paths or duplicate database paths across targets; throw `std::runtime_error` with a clear message.
- **Outcome**: Added post-parse validation block in `config/Config.cpp` using two `std::unordered_set<std::string>` to detect duplicates in O(n). Added `<unordered_set>` include. Error messages use colon separator: `"Config: duplicate root path: <path>"` and `"Config: duplicate database path: <path>"`. Build clean (`config_lib` target passes).
- **Next Step**: Config parser tests (task #2 / B1).

## [2026-04-07 11:30] - Phase 0014: imagestore Progress Metrics Display

- **Agent**: cpp-spec-coder
- **Task**: Implement phase 0014 — add `-q`/`--graph` flags and a `ProgressReporter` background-thread class to `imagestore` per `docs/plan/0014.DISPLAY.md`.
- **Outcome**: All implementation tasks completed. New files: `imagestore/Stats.h` (extracted struct in `imagestore` namespace), `imagestore/DisplayMode.h` (enum class), `imagestore/Output.h` (inline `g_outputMutex` + `stderrLine`), `imagestore/ProgressReporter.h` + `ProgressReporter.cpp` (background thread, Normal/Graph/Quiet modes, ANSI cursor hide/show, TTY guard, terminal width via `ioctl`). `main.cpp` updated: removed `ProgressTracker`, uses new headers, `-q`/`--graph` flags with mutual-exclusion validation, `ProgressReporter` wired in. `imagestore/CMakeLists.txt` updated to add `ProgressReporter.cpp`. Build clean, all 8 tests pass.
- **Next Step**: Integration testing of Normal, Quiet, and Graph modes on a live run (task #10, #11).

## [2026-04-07 10:00] - Phase 0013: Runtime Pipeline Progress Tracking

- **Agent**: cpp-spec-coder
- **Task**: Implement phase 0013 — add per-stage counters, byte counters, in-flight gauges, GaugeGuard/SizedGaugeGuard RAII helpers, pipeline progress snapshot table, and `addFile` entry point per `docs/plan/0013.PROGRESS.md`.
- **Outcome**: All 7 implementation steps were already complete in the codebase — `GaugeGuard`/`SizedGaugeGuard` in `metrics/Gauge.h`, new Counter/Gauge/Histogram members in `metrics/Metrics.h`, `snapshot()`/`reset()` updated in `metrics/Metrics.cpp`, pipeline progress table in `metrics/Snapshot.cpp`, full instrumentation in `imager/Imager.cpp` (`addImage` + `addFile`), and `addFile` declaration in `imager/Imager.h`. Build clean, all 8 tests passed (DatabaseTests, jpeg, png, heic, nef, mov, aae, ImagerTests).
- **Next Step**: No further action required for this phase.

## [2026-04-05 18:00] - Fix Include Ordering and Angle-Bracket Violations (M1)

- **Agent**: cpp-spec-coder
- **Task**: Fix include ordering and angle-bracket violations across 15 files in `imager/imager/` per the M1 coding standards remediation task.
- **Outcome**: All cross-module headers converted from quoted to angle-bracket form. Group ordering corrected (own header → project modules with `<>` → stdlib → same-dir private `""`). Files changed: `Imager.h`, `Imager.cpp`, `FileStorage.h`, `FileStorage.cpp`, `MultiDatabase.h`, `MultiDatabase.cpp`, `Hasher.h`, `Validators.h`, `sample/main.cpp`, `JpegValidatorImpl.cpp`, `PngValidatorImpl.cpp`, `HeicValidatorImpl.cpp`, `NefValidatorImpl.cpp`, `MovValidatorImpl.cpp`, `AaeValidatorImpl.cpp`. The 6 `*ValidatorImpl.cpp` files now use the new namespaced paths (`<validations/jpeg/jpeg_validator.h>`, etc.) matching the updated CMakeLists include roots. Verification grep confirms zero remaining quoted cross-module includes in `imager/imager/` (test files excluded from M1 scope). `clang-format` needs to be run on all 15 files (requires Bash permission).
- **Next Step**: Grant Bash permission to run `clang-format -i` on all 15 files and then `cmake --preset default && cmake --build --preset default` to verify the build.

## [2026-04-05 17:00] - Remove Metrics Singleton, Add DI (C3 + H4)

- **Agent**: cpp-spec-coder
- **Task**: Remove `metrics::Metrics::get()` Meyer's singleton and replace with dependency injection. Add explicit move=delete (H4) to `Metrics`, `FileStorage`, `MultiDatabase`, and `Imager`.
- **Outcome**: Singleton fully removed. `Imager::Impl` now owns `metrics::Metrics metrics` as its first member (declared before `pool`/`dbs`/`storage` to satisfy C++ member-init order). `FileStorage` and `MultiDatabase` constructors each accept `metrics::Metrics&` and store it as `m_metrics`. All `metrics::Metrics::get().foo` call sites replaced with `m_metrics.foo` (FileStorage/MultiDatabase) and `m_impl->metrics.foo` (Imager). Lambdas inside `addImage` that needed metrics were updated to thread `metrics::Metrics& m` as an explicit parameter. `blob/Blob.h` and `coro/ThreadPool.h` had `#ifdef IMAGER_METRICS_ENABLED` blocks removed since the singleton they relied on no longer exists. `imager/sample/main.cpp` updated to use new `Imager::metrics()` accessor. `grep -r 'Metrics::get()' --include='*.cpp' --include='*.h'` returns only a comment in `Timer.h`, updated to reflect new usage pattern.
- **Next Step**: Build verification (`cmake --preset default && cmake --build --preset default`), then continue with remaining CODING_FIXES.md items.

## [2026-04-05 15:30] - CLAUDE.md Documentation Fix (C2 + L8)

- **Agent**: cpp-spec-coder
- **Task**: Clarify SQLite as an intentional system dependency (coding standard fix C2) and correct stale project tree (fix L8: remove nonexistent `imager/src/` subdirectory).
- **Outcome**: Three edits to `/home/vibe/src/imager/CLAUDE.md`: deps table SQLite row now says "intentional: no bundled copy"; `database/` tree comment updated to "intentional system dep"; `imager/` tree flattened to remove the `src/` nesting level that never existed on disk. `database/CLAUDE.md` already correct, no change needed.
- **Next Step**: Continue with remaining CODING_FIXES.md items.

## [2026-04-05 14:00] - Codebase Audit Against CODING.md

- **Agent**: cpp-spec-coder
- **Task**: Analyze the entire imager codebase against the coding standards defined in CODING.md and document all violations in CODING_FIXES.md (analysis only, no code changes).
- **Outcome**: Completed full scan of all first-party source files. Found 12 categories of violations spanning formatting, include ordering, singleton usage, bundled library strategy for SQLite, raw resource management in Hasher.cpp and validate_png.cpp, missing -Werror in CMakeLists, static file-local helpers that should be anonymous namespaces, bare catch(...) in non-rollback paths, and graceful CppUnit skip policy not uniformly applied. All findings documented in /home/vibe/src/imager/CODING_FIXES.md.
- **Next Step**: Fix the violations documented in CODING_FIXES.md file-by-file.

## [2026-04-11 10:00] - Replace manual file-read boilerplate with addFile in imagestore

- **Agent**: cpp-spec-coder
- **Task**: Modify imagestore/main.cpp worker lambda to use `img.addFile(capturedPath)` for the non-dry-run path instead of manual stat+read+blob+addImage steps.
- **Outcome**: Replaced the non-dry-run code path with a two-step approach: `fs::file_size` for `stats.totalBytes` accounting (same error handling as before), then `img.addFile(capturedPath)` for the full pipeline. The dry-run path retains the original blob-reading code unchanged. Result-handling switch unchanged. Build clean, all 10 tests pass.
- **Next Step**: None specified.
