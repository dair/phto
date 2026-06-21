# Work Log

## [2026-04-12 11:00] - Verbose/Normal output modes tests (spec 0016)
- **Agent**: test-spec-writer
- **Task**: Write comprehensive tests for the verbose and normal output modes redesign (spec 0016). Task #4 from the imagestore-output-redesign team.
- **Outcome**: (1) Fixed stale `--graph` tests (tests 3-6) in `imagestore/test/test_cli.sh.in` — they were passing for the wrong reason (`--graph` as unknown flag exits 1, and `printUsage()` contained "mutually exclusive"). Replaced with correct `-v -q` mutual exclusion tests. (2) Added 20 new shell tests covering: flag recognition, normal/quiet/verbose modes, no `[progress]` lines in normal mode, quiet mode produces zero output, verbose non-TTY notice, OK/DUP/ERR/SKIP result line formats with real images, `--dry-run` summary format, exit code 2 on errors. (3) Added `imagestore/test/OutputTest.cpp` — 21 CPPUnit unit tests for `SlotTracker` (14), `ResultLog` (5), and `PipelineStage` names (2). Wired into `imagestore/CMakeLists.txt` using `find_library(CPPUNIT_LIB cppunit)` pattern from other test modules. All 14 ctest suites pass (0 failures). Created `docs/testing/verbose-output.md` and updated `docs/testing/README.md` and `docs/testing/imagestore-cli.md`.
- **Next Step**: Verbose TTY ANSI rendering cannot be tested without a real PTY. Comma-separated thousands in statistics format not yet enforced by a test. Stage visibility in `--dry-run` + `-v` not automatically verifiable without TTY.

## [2026-04-11 14:30] - imagestore CLI shell tests
- **Agent**: test-spec-writer
- **Task**: Write CLI tests for the `imagestore` binary covering argument validation, mutual exclusion, help text, and config-not-found. Use shell scripts invoked via CMake `add_test`.
- **Outcome**: Created `imagestore/test/test_cli.sh.in` with 12 shell tests and updated `imagestore/CMakeLists.txt` to configure, chmod, and register the script as `imagestore_cli_tests`. All 11 ctest suites pass (10 existing + new). Created `docs/testing/imagestore-cli.md` and updated `docs/testing/README.md`. Key fix: wrapped each test in a subshell to avoid `set -e` aborting the script on the first failure.
- **Next Step**: Error-file skip behaviour and dry-run mode tests remain untested; both require a real config with valid storage roots — out of scope for headless CI.

## [2026-04-11 10:00] - Metrics library CPPUnit tests (MetricsTest.cpp)
- **Agent**: test-spec-writer
- **Task**: Write CPPUnit tests for all five metrics primitives (Counter, Gauge, Histogram, Timer, Snapshot/format) plus Metrics registry snapshot and reset. Task #10 (2.6 / B4, B9).
- **Outcome**: Created `/home/vibe/src/imager/metrics/test/MetricsTest.cpp` with 7 fixture classes and 44 tests. Created `metrics/test/CMakeLists.txt`. Wired `add_subdirectory(test)` into `metrics/CMakeLists.txt`. All 10 ctest targets pass (0 failures). Created `docs/testing/metrics-primitives.md` and updated `docs/testing/README.md`.
- **Next Step**: Concurrency stress tests for individual atomic primitives are not yet covered; low priority given the lock-free design is verified by the existing atomic semantics.

## [2026-04-08 00:00] - Phase B integration tests (MultiTargetTest.cpp)
- **Agent**: test-spec-writer
- **Task**: Write Phase B integration tests (B6, B7, B8, B10) for the imager project in a new file `MultiTargetTest.cpp`; register in CMakeLists.
- **Outcome**: Created `/home/vibe/src/imager/imager/test/MultiTargetTest.cpp` with 3 test classes and 5 tests. Updated CMakeLists.txt to include the new file. All 42 tests pass (0 warnings). Created `docs/testing/phase-b-multi-target.md` and `docs/testing/README.md`.
- **Next Step**: Additional Phase B criteria (B1–B5, B9) not yet covered; consult `docs/plan/0015.GAPS.md` for remaining gaps.

## [2026-04-14 16:30] - Memory regression test + Valgrind validation (Tasks #5, #7, #8)

- **Agent**: test-spec-writer
- **Task**: Phase 3 Valgrind validation, add Valgrind memory regression test to CTest, run full 15-suite test suite.
- **Outcome**: (1) Confirmed WhenAll SubTask fix eliminated all definite/indirect leaks — all 5 original Valgrind scenarios (baseline, new-file, duplicate, mixed, dry-run) report 0 bytes definitely/indirectly lost, 0 errors. (2) Created `imager/valgrind.supp` suppressing 35,496 bytes of third-party still-reachable memory (libglib, libgobject, libgomp, libheif, libav*) via broad `obj:*` patterns. (3) Created `imagestore/test/test_memcheck.sh.in` and added `imagestore_memcheck` CTest entry (guarded by `find_program(VALGRIND)`, 120s timeout). Test exercises two Valgrind scenarios: new-file import (Leaks A+B+C path) and duplicate import (Leak A path). Any definite/indirect leak fails with exit 1. (4) Full 15-suite CTest run: 15/15 passed, 0 failures, 13s total.
- **Next Step**: None — all tasks complete, zero regressions confirmed.

## [2026-04-08 10:00] - CPPUnit tests for config parser

- **Agent**: test-spec-writer
- **Task**: Write CPPUnit unit tests for `config/Config.cpp` covering 10 specified test cases: valid single/two-target parsing, missing file, empty file, malformed TOML, missing `root`, missing `database`, empty targets array, duplicate root paths, duplicate database paths.
- **Outcome**: Created `/home/vibe/src/imager/config/test/ConfigTest.cpp` (3 test fixtures, 10 tests total) and `/home/vibe/src/imager/config/test/CMakeLists.txt`. Updated `config/CMakeLists.txt` with `add_subdirectory(test)`. All 10 tests pass (`OK (10 tests)`, 0.01s).
- **Next Step**: Update `docs/testing/` status files for the config feature if tracking is desired.
