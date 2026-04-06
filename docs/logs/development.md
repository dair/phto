# Work Log

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
