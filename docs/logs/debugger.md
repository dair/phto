# Work Log

## [2026-04-07 14:00] - Test suite verification for plan 0013 (Runtime Pipeline Progress Tracking)
- **Agent**: cpp-debugger
- **Task**: Verify all 8 tests pass for the plan 0013 implementation (GaugeGuard, SizedGaugeGuard, 25 new metrics members, addFile, pipeline instrumentation)
- **Outcome**: All 8 tests pass cleanly in 2.58s. Build succeeded with zero warnings (warnings-as-errors enabled). Test results: DatabaseTests (45), jpeg_validator_tests (7), test_validate_png (6), heic_validator_tests (11), nef_validator_tests (10), mov_validator_tests (11), aae_validator_tests (9), ImagerTests (37). One latent gauge-leak risk identified in addFile: if Blob(fileSize) throws std::bad_alloc after inflight_reading.increment() but before inflight_reading_bytes is incremented, the file gauge leaks. The bytes gauge is not yet set so it is not affected. This is a pre-existing design note in the plan (the guard is manual by design to handle the split increment order), not a test failure.
- **Next Step**: No action required — tests are healthy. The gauge-leak edge case is an OOM corner case that cannot be triggered under normal test conditions.

## [2026-04-14 15:00] - Phase 1: RelWithDebInfo Build + Valgrind Baseline (Tasks #1, #2)
- **Agent**: Debugger
- **Task**: Build imager with RelWithDebInfo and establish clean Valgrind baseline; verify suppression file
- **Outcome**: Built RelWithDebInfo binary at `/tmp/imager-build-debugger/imagestore/imagestore` (with debug_info, not stripped). Ran all 5 Valgrind scenarios WITHOUT suppressions — found **0 definite leaks, 0 errors** across all runs. The WhenAll SubTask fix (2026-04-14 12:30 in orchestrator.md) already resolved all coroutine frame leaks that LEAKING.md documented. Still-reachable is constant 35,496 bytes / 237 blocks (all third-party). Existing `valgrind.supp` correctly suppresses all 237 still-reachable blocks — confirmed with suppressed run showing 0 visible, 35,496 suppressed.
- **Next Step**: Tasks #5, #6 (Valgrind + ASan validation post-blockOn fix)

## [2026-04-14 15:30] - Phase 3 Tasks #5 + #6: Valgrind + ASan Validation
- **Agent**: Debugger
- **Task**: Validate blockOn fix with Valgrind (5 scenarios) and cross-check with AddressSanitizer
- **Outcome**: Key finding — the coroutine frame leaks documented in LEAKING.md were already resolved by the WhenAll SubTask fix before Tasks #5/#6 were unblocked. Valgrind results (RelWithDebInfo, with suppression file):

  | Run | Scenario | Definite Lost | Indirect Lost | Errors | Suppressed |
  |-----|----------|--------------|--------------|--------|------------|
  | 1 | Empty stdin | 0 B | 0 B | 0 | 35,496 B (237 blks) |
  | 2 | 1 new JPEG | 0 B | 0 B | 0 | 35,496 B (237 blks) |
  | 3 | 1 duplicate JPEG | 0 B | 0 B | 0 | 35,496 B (237 blks) |
  | 4 | 3 files mixed | 0 B | 0 B | 0 | 35,496 B (237 blks) |
  | 5 | Dry-run 1 JPEG | 0 B | 0 B | 0 | 35,496 B (237 blks) |

  ASan results (separate build, `detect_leaks=1`, no ASan log files produced):
  - All 5 scenarios exited with expected codes (0, 0, 0, 2, 0)
  - No heap-use-after-free, no heap-buffer-overflow, no invalid reads
  - No LeakSanitizer reports
  - ASan binary linked against libasan.so.8 (confirmed)

  **Both Valgrind and ASan pass. The codebase is memory-clean without requiring any blockOn changes.**

- **Next Step**: Report to team-lead. Tasks #3 and #4 (blockOn fix) may still be desirable as a defensive cleanup (explicit frame destruction scope), but they are NOT required to achieve a leak-free result.

## [2026-11-01 00:17] - Plan 0018: memory-growth investigation, PNG validator longjmp leak
- **Agent**: Debugger (mi-dbg)
- **Task**: Determine whether imagestore RSS grows during long imports and which subsystem retains the memory (plan 0018 H1-H5).
- **Outcome**: Healthy imports (10,080 distinct JPEG/PNG/MOV incl. >8 MB streamed files) show no growth: RSS plateaus at 88 MiB (--jobs 1) / 136 MiB (default jobs); blobs flat. Growth found only on failure paths: truncated PNGs leaked ~1.2 KB each (47 -> 106 MiB over 60k, blobs flat). Root cause: `std::vector row` constructed after `setjmp` in `png_detail::validatePng` and `PngStreamValidatorImpl::validateFile`; libpng's longjmp skipped its destructor (UB + leak). Valgrind confirmed (36,000 B / 30 blocks definitely lost). JPEG validators audited: clean (no non-trivial locals after setjmp; libjpeg pool memory). Fix: hoist `row` above `setjmp`, `resize` after. Added `validations/png/test/testimg.png` and memcheck scenario 5 (truncated PNG/JPEG under valgrind; verified it fails on the old code). After fix: 60k truncated PNGs flat at 35 MiB; ctest 42/42.
- **Next Step**: Commit by team-lead; mi-docs records results in memory.md / plan 0018. HEIC/NEF success sets and --jobs scaling beyond 1/default were not measured.

## [2026-11-01 01:20] - Plan 0018 follow-up: HEIC/NEF success + failure sets, --jobs 4 (measurement only)
- **Agent**: Debugger (mi-dbg)
- **Task**: Close H3/H4 for libheif/LibRaw and one more --jobs value; no code changes.
- **Outcome**: Distinct HEIC/NEF made by zero-padding the fixtures (HEIC 2.7 MB, NEF 5.4 MB each). No unbounded growth. HEIC working set is large but bounded: --jobs 1, 4,500 files (1,500 added + 3,000 dup) 1.24 GiB -> 1.7 GiB by ~500 s, then flat (1.68 GiB at end); default jobs (8) 1.7-2.0 GiB oscillating over 4,500 files; mixed HEIC/NEF/MOV/JPEG --jobs 4, 20,000 files: 1.6 -> ~1.8 GiB plateau. NEF --jobs 1, 2,000 files 83 -> 145 MiB (one step, then flat); default jobs 231 MiB flat. Failure paths: 10,000 truncated NEF (all errors) 115 -> 134 MiB flat after warm-up; 5,000 small-truncated HEIC (1,240 errors) ~210-240 MiB, no trend. Side observation: libheif accepts many truncated HEIC files as valid (2,000 truncated at 5-95% of size: 0 errors; small truncations: 3,760 of 5,000 accepted). Not a memory issue; validator leniency, unconfirmed whether intended.
- **Next Step**: Corpus, targets, generators deleted. Report to team-lead and mi-docs.

## [2026-11-01 02:00] - Plan 0018: HEIC 1.7 GiB RSS explained (glibc arena fragmentation); measurement only
- **Agent**: Debugger (mi-dbg)
- **Task**: Explain ~1.7 GiB RSS at --jobs 1 for HEIC.
- **Outcome**: Fixture valid.heic is 3024x4032 8-bit (12.2 MP, 1 top-level image, no alpha; RGB output 36.6 MB). libheif 1.21.2, libde265 1.0.16; heif_context_set_max_decoding_threads exists (libheif/heif_decoding.h) and neither validator calls it. A standalone single-thread decode loop (40x, no imagestore) shows RSS after free growing 52 -> 713 MiB with default settings (49 threads), flat 89 MiB with max_decoding_threads=0 (3x slower per decode: 23.7 s vs 7.6 s per 41), flat at 90 MiB with MALLOC_ARENA_MAX=1, 111 MiB at 2, 152 at 4, 202 at 8, same 7.6 s speed; threads=1 and 4 still 927/754 MiB; malloc_trim after each decode only reduced it (431 MiB). So the growth is per-thread malloc arena retention/fragmentation from libheif/libde265 worker threads allocating tile/plane buffers that the main thread frees, not a leak. End-to-end imagestore, 800 distinct padded HEIC: --jobs 1 1.0-1.35 GiB (baseline) vs 141-176 MiB with MALLOC_ARENA_MAX=2 (same 156 s); default jobs (8) 615 MiB with MALLOC_ARENA_MAX=2 vs ~1.7-2.0 GiB before, 56 s.
- **Next Step**: Proposed fix to team-lead: mallopt(M_ARENA_MAX, 2) at start of imagestore/imagerd main(); awaiting OK. No product code changed.

## [2026-11-01 02:40] - Plan 0018: malloc arena cap implemented (HEIC memory fix)
- **Agent**: Debugger (mi-dbg)
- **Task**: Apply approved fix for HEIC RSS (glibc per-thread arenas).
- **Outcome**: Added header-only `imager/MallocTuning.h` (`imager::capMallocArenas()`: `mallopt(M_ARENA_MAX, 2)` under `__GLIBC__`, skipped when `MALLOC_ARENA_MAX` is set), called first in `main()` of imagestore and imagerd. New test `imagestore_heic_rss_test` (imagestore/test/test_heic_rss.sh.in, ~8 s): 40 distinct padded HEICs at --jobs 1 must keep hwm < 400 MiB; with the call commented out it fails (hwm 1 GiB), with it passes. ctest 43/43. End-to-end, 800 distinct HEIC: --jobs 1 144-167 MiB (was 1.0-1.35 GiB), same 155 s; default jobs 558-613 MiB (was ~1.7-2.0 GiB), 56 s, 14 files/s.
- **Next Step**: DEPLOYMENT.md wording to mi-docs; team-lead commits.
