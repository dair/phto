# Work Log

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
