# Valgrind Baseline — RelWithDebInfo Build

**Date**: 2026-04-14
**Build**: `/tmp/imager-build/imagestore/imagestore` (RelWithDebInfo, not stripped)
**Tool**: Valgrind 3.25.1
**Flags**: `--leak-check=full --track-origins=yes --show-leak-kinds=all --num-callers=20`

## Test Setup

- Working directory: `/tmp/imagestore-vg/`
- Config: `[[targets]] root = store, database = store/imager.db`
- Test images:
  - `testimg.jpg` — 5,770 bytes (from `validations/jpeg/test/testimg.jpg`)
  - `minimal.jpg` — 159 bytes (synthetic minimal JPEG)
  - `/tmp/nonexistent_xyz_99.jpg` — deliberate missing path

## Results

| Run | Scenario                       | Exit | Definite | Indirect | Possibly | Still Reachable | Errors |
|-----|--------------------------------|------|----------|----------|----------|-----------------|--------|
| 1   | Empty stdin                    | 1*   | 0 B      | 0 B      | 0 B      | 35,496 B        | 0      |
| 2   | Single new JPEG                | 0    | 0 B      | 0 B      | 0 B      | 35,496 B        | 0      |
| 3   | Duplicate JPEG (already stored)| 0    | 0 B      | 0 B      | 0 B      | 35,496 B        | 0      |
| 4   | Mixed (dup + new + missing)    | 2    | 0 B      | 0 B      | 0 B      | 35,496 B        | 0      |
| 5   | Dry-run, single JPEG           | 0    | 0 B      | 0 B      | 0 B      | 35,496 B        | 0      |

*Run 1 exit=1 was produced earlier with a malformed config; rerun with correct config also produces clean leak numbers.

## Observations

- **All 5 scenarios show 0 definitely-lost, 0 indirectly-lost, 0 possibly-lost bytes.**
- Still-reachable of 35,496 bytes is constant across all runs — matches LEAKING.md attribution to libglib-2.0 / libgobject-2.0 / libgomp third-party globals.
- The coroutine-frame definite leaks described in LEAKING.md (Leaks A, B, C totalling ~6.3 KB per file in Leak A) **do not manifest in this RelWithDebInfo build.** Possible reasons:
  - The prior investigation used a Debug or non-RelWithDebInfo build with different coroutine code generation.
  - A fix was landed between the LEAKING.md investigation and this baseline.
  - Compiler-version differences in coroutine frame lifetime elision.

## Artifacts

| Artifact | Location |
|----------|----------|
| Run 1 log | `/tmp/vg-run1.txt` |
| Run 2 log | `/tmp/vg-run2.txt` |
| Run 3 log | `/tmp/vg-run3.txt` |
| Run 4 log | `/tmp/vg-run4.txt` |
| Run 5 log | `/tmp/vg-run5.txt` |
| Test dir  | `/tmp/imagestore-vg/` |
| Build dir | `/tmp/imager-build/` |
