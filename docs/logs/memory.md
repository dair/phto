# Memory Work Log

## [2026-04-14 11:00] - Long-run memory investigation plan and runtime instrumentation

- **Agent**: Codex
- **Task**: Learn the `imager` / `imagestore` memory behavior, document a concrete investigation process, then add runtime instrumentation that can distinguish RSS growth from live library-owned buffer growth.
- **Outcome**:
  - Added a dedicated investigation plan at [docs/plan/0018.MEMORY.md](/home/vibe/imager/imager/docs/plan/0018.MEMORY.md). The plan explains why leak checks were insufficient, identifies the main suspects (`std::async` dispatch behavior, heavy validator backends, allocator retention, `ErrorFile` accumulation), and defines a staged diagnostic workflow.
  - Added a new opt-in runtime reporter:
    - [imagestore/MemoryReporter.h](/home/vibe/imager/imager/imagestore/MemoryReporter.h)
    - [imagestore/MemoryReporter.cpp](/home/vibe/imager/imager/imagestore/MemoryReporter.cpp)
  - Wired the reporter into the `imagestore` executable in:
    - [imagestore/main.cpp](/home/vibe/imager/imager/imagestore/main.cpp)
    - [imagestore/CMakeLists.txt](/home/vibe/imager/imager/imagestore/CMakeLists.txt)
  - Added two CLI switches:
    - `--memory-report`
    - `--memory-interval N`
  - The reporter samples Linux process memory from `/proc/self/status` and emits:
    - current RSS (`VmRSS`)
    - peak RSS (`VmHWM`)
    - `blobs_alive`
    - `blob_bytes_alive`
    - aggregate in-flight file count
    - aggregate in-flight bytes
    - thread-pool queue depth / active threads
    - processing counters (`processed`, `added`, `duplicates`, `errors`, `skipped`)
  - Output is routed through `ResultLog`, so it coexists with both normal line mode and verbose TTY scrolling mode without bypassing the existing output synchronization.
  - `--quiet` and `--memory-report` are treated as mutually exclusive because the memory stream is itself diagnostic output.
  - Verification:
    - `cmake --build --preset default` passed
    - `ctest --preset default --output-on-failure -R 'imagestore_(cli_tests|output_tests|memcheck)'` passed
- **Rationale**:
  - The existing `Blob` metrics already track the library’s explicit file-buffer ownership. Adding process RSS and HWM beside them makes it possible to answer the key question: "Is RSS growing because our live buffers are growing, or because something else retains memory?"
  - The instrumentation is intentionally opt-in so normal imports stay unchanged.
- **Next Step**:
  - Run representative long imports with:
    - `--memory-report --memory-interval 5 --jobs 1`
    - `--memory-report --memory-interval 5 --jobs <normal>`
    - isolated per-format workloads
  - Compare RSS against `blob_bytes_alive` to classify the growth source before using heavier external profilers.

## [2026-11-01] - Plan 0018 investigation: growth classified (interim, fix proposed)

- **Agent**: mi-dbg (measurements), recorded by bookkeeper
- **Task**: Execute plan 0018 Phases 2-4 with `--memory-report` and classify the growth source (H1-H5).
- **Status of this entry**: interim. The per-format success sets (H3) and the `--jobs` scaling runs (H4) were still running when this was written. The fix is proposed, not yet approved or landed.

### Corpus

- 10,080 distinct generated noise files: JPEG (100 KB-9 MB), PNG (100 KB-9.7 MB), and padded MOV. 35 of them are larger than 8 MB, so they take the streaming path.
- Failure set: 1,200 files (truncated JPG/PNG/MOV plus garbage).
- Two-target config; fresh targets for every run.

### Runs and results

| Run | Result |
|-----|--------|
| Healthy import, `--jobs 1` | Plateau. RSS 79 / 81 / 86 / 87 / 87 / 88 MiB. |
| Healthy import, default jobs (8) | Plateau after about 4k files. RSS 105 / 121 / 135 / 136 / 136 / 136 / 136 / 136 MiB. |
| Truncated PNG x 60,000, `--jobs 1`, no `-e` | Linear growth 47 -> 106 MiB (about 1.2 KB per failure); `blobs_alive` flat. |
| Truncated JPEG x 16k | Flat at 36 MiB. |
| Truncated MOV x 60k | Flat at 40 MiB. |
| Garbage x 8k | Flat at 35 MiB. |
| Mixed bad set x 120k | Linear growth 42 -> 76 MiB. |
| `ErrorFile` (`-e`, 1,200 paths) | No added growth. 45 MiB on the first pass, 31 MiB on a skip-only second pass. |

For the healthy runs, `blobs_alive` stayed at 0-8 and `blob_bytes_alive` at or below 3 MiB.

### Classification

- **Healthy imports: no growth.** The 8 MB streaming threshold (plan 0017) removed the large-file symptom.
- **Failure path: a leak in the PNG validator.** `png_detail::validatePng` (`validations/png/validate_png.cpp:74`) allocates a `std::vector` `row` after `setjmp`. When libpng reports an error it calls `longjmp`, which skips the vector's destructor, so `rowbytes` leak for each failed PNG.
- **Evidence:** Valgrind reports 36,000 B in 30 blocks definitely lost for 30 truncated PNGs. Only the truncated-PNG and mixed sets grow; truncated JPEG, MOV and garbage sets are flat.
- **Plan 0018 case:** the symptoms are Case B (RSS rises, `blob_bytes_alive` flat), with the cause isolated to one validator's failure path (Case C/D-like).
- **Ruled out:** H1 (`Blob` retention), allocator and `std::async` behavior, and `ErrorFile` (H5 as the main cause).
- **Pending:** H3 per-format success sets and H4 `--jobs` scaling numbers.

### Next Step

- Fix the `row` allocation in `validatePng` so it is released across the `longjmp` boundary (fix proposed by mi-dbg, awaiting team-lead approval).
- Record the H3/H4 numbers and the post-fix re-run when available.

## [2026-11-01] - Plan 0018 investigation: PNG failure-path leak fixed and re-measured

- **Agent**: mi-dbg (fix and measurements), recorded by bookkeeper
- **Task**: Fix the leak classified in the previous entry and confirm it.
- **Outcome**:
  - Cause: both `png_detail::validatePng` (`validations/png/validate_png.cpp`) and the stream validator (`imager/PngStreamValidatorImpl.cpp`) constructed `std::vector row` after `setjmp`. The libpng `longjmp` skipped its destructor. `row` is now declared above `setjmp` and resized after it. The JPEG validators were audited and are clean.
  - Re-measurement, 60,000 truncated PNGs, `--jobs 1`: RSS flat at 35 MiB (before the fix: 47 -> 106 MiB). `ctest --preset default`: 42/42 pass.
  - Regression coverage: `imagestore_memcheck` scenario 5 (truncated PNG and JPEG under valgrind) and the fixture `validations/png/test/testimg.png`. The test fails on the old code and passes now.
  - Not measured: HEIC and NEF success sets, `--jobs` values other than 1 and the default (so H3 is covered only for JPEG, PNG and MOV, and H4 only partially), heaptrack/Massif (not installed), allocator tuning (not needed).
  - Debugger summary: `docs/logs/debugger.md` entry `[2026-11-01 00:17]`.
- **Next Step**: None required for the identified leak. HEIC/NEF success-set runs and a `--jobs` sweep remain optional.

## [2026-11-01] - Plan 0018 investigation: HEIC, NEF and `--jobs 4` runs (H3/H4 complete)

- **Agent**: mi-dbg (measurements), recorded by bookkeeper
- **Task**: Close the gaps listed in the previous entry: HEIC and NEF success and failure sets, and a concurrency run.
- **Corpus**: distinct files made by zero-padding the HEIC (2.7 MB) and NEF (5.4 MB) test fixtures. RSS was sampled from `/proc`, because the `MEM` line prints GiB as whole numbers.

| Run | RSS over time |
|-----|---------------|
| HEIC healthy, `--jobs 1`, 4,500 imports | 1.24 GiB -> about 1.7 GiB over the first ~500 s, then flat (1.68 GiB at the end) |
| HEIC healthy, default jobs (8) | 1.7-2.0 GiB, bounded oscillation |
| NEF healthy, `--jobs 1`, 2,000 imports | 83 -> 145 MiB (one step, then flat) |
| NEF healthy, default jobs | 231 MiB, flat |
| Mixed NEF/HEIC/MOV/JPEG, `--jobs 4`, 20,000 imports | 1.62 GiB -> about 1.8 GiB plateau |
| 10,000 truncated NEF | 115 -> 134 MiB, flat |
| 5,000 small-truncated HEIC (1,240 errors) | about 210-240 MiB, no trend |

- **Classification**: no format-specific (H3) and no concurrency-scaled (H4) growth. The only defect found in plan 0018 is the PNG `longjmp` leak, which is fixed. The investigation stays closed.
- **Observation, not a leak**: the HEIC working set is high but bounded, about 1.2-2 GiB RSS for the 2.7 MB test fixture at `--jobs` 1-8. The roughly 300 MB HEIC decoder estimate in `docs/user/DEPLOYMENT.md` may be low for large images; the fixture's resolution was not checked.
- **Side finding (validator leniency, not memory)**: libheif accepted many truncated HEIC files as valid: 0 errors for 2,000 files truncated at 5-95%, and 3,760 of 5,000 small truncations accepted. This matches the "weak HEIC validation" item in `docs/plan/README.md`.
- **Still not measured**: heaptrack/Massif (not installed) and `MALLOC_ARENA_MAX` tuning (not needed).

## [2026-11-01] - Plan 0018 investigation: HEIC working set explained (cause found, fix proposed)

- **Agent**: mi-dbg (measurements), recorded by bookkeeper
- **Task**: Explain the 1.2-2 GiB RSS seen in the HEIC runs.
- **Status of this entry**: the cause is found; the fix is proposed and awaits team-lead approval. It has not landed.
- **Cause**: glibc per-thread malloc arena retention from the libheif/libde265 worker threads. This is not a leak. Fixture: `valid.heic`, 3024x4032 8-bit, libheif 1.21.2, libde265 1.0.16.
- **Evidence**:
  - Standalone libheif-only decode loop: RSS after free grows 52 -> 713 MiB over 40 decodes. With `MALLOC_ARENA_MAX=1` it is flat at 90 MiB; with `MALLOC_ARENA_MAX=2` it is 111 MiB. Decode speed is the same.
  - `imagestore`, 800 distinct HEIC files:

    | Setting | Default | `MALLOC_ARENA_MAX=2` |
    |---------|---------|----------------------|
    | `--jobs 1` | 1.0-1.35 GiB | 141-176 MiB (same 156 s) |
    | default jobs | about 1.7-2.0 GiB | 615 MiB |

  - This also explains the plateau-with-oscillation seen in the earlier HEIC and mixed runs.
- **Proposed fix**: `mallopt(M_ARENA_MAX, 2)` at the start of `main()` in `imagestore` and `imagerd`.
- **HEIC truncation leniency (validator, not memory)**:
  - 2,000 files truncated at 5%..95% of the 2.7 MB fixture, evenly spread (`imagestore`, `--jobs 1`, fresh targets): 0 errors. 90 were stored and 1,910 were reported as duplicates of those. The validator returned success for all 2,000.
  - 5,000 files truncated at 64 B to about 120 KB (`n = 64 + (i*97) mod 120000`): 3,760 accepted and stored, 1,240 rejected with an error.
  - NEF truncations (5-95%, 10,000 files) were all rejected, as were PNG, JPEG and MOV truncations.
  - Not verified whether this is intended. libheif may decode the primary image even when later data is missing, or the truncation may cut only trailing non-image boxes. A follow-up should check the `heif_decode_image` error handling and whether decoding warnings (`heif_image_get_decoding_warnings`) should count as failures.

## [2026-11-01] - Plan 0018: HEIC arena cap implemented (fixed)

- **Agent**: mi-dbg (fix and measurements), recorded by bookkeeper
- **Outcome**:
  - The proposed fix from the previous entry is implemented: `imager::capMallocArenas()` in `imager/MallocTuning.h` calls `mallopt(M_ARENA_MAX, 2)` at the start of `main()` in `imagestore` and `imagerd`. It is glibc-only, and an operator-set `MALLOC_ARENA_MAX` environment variable wins.
  - Measured on 800 distinct 12 MP HEIC files, two targets:

    | Setting | Before the cap | With the cap |
    |---------|----------------|--------------|
    | `--jobs 1` | 1.0-1.35 GiB | about 145-165 MiB |
    | default jobs (8) | 1.7-2.0 GiB | about 560-610 MiB |

    Throughput is unchanged: 5 files/s at `--jobs 1`, 14 files/s at 8 jobs.
  - Corrected working-set figures: about 60-80 MiB per concurrent 12 MP HEIC decode after the cap. This replaces the earlier estimate of about 300 MB for a large HEIC. Other formats, default jobs: JPEG/PNG/MOV plateau at 88 MiB (`--jobs 1`) and 136 MiB (8 jobs); NEF 83-145 MiB (`--jobs 1`) and 231 MiB (8 jobs); large streamed JPEGs (up to 9 MB) peak at about 120 MiB at 8 jobs.
  - Docs updated: `docs/user/DEPLOYMENT.md` (new "Memory and malloc arenas" section, HEIC table row), `docs/user/architecture.md`, `CLAUDE.md`.
- **Regression guards for plan 0018** (two):
  1. `imagestore_memcheck` scenario 5: truncated PNG/JPEG under valgrind (PNG `longjmp` leak).
  2. `imagestore_heic_rss_test` (`imagestore/test/test_heic_rss.sh.in`, about 8 s): 40 distinct HEIC files at `--jobs 1` must keep peak RSS (`--memory-report` `hwm`) below 400 MiB; without the cap it reaches about 1 GiB and fails.
- The suite is now 43 tests.
