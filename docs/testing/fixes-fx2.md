# Testing: Fix step fx2 (HEIC truncation, duplicate id, memory-report shutdown)

**Plan Reference**: `docs/plan/README.md` findings 5 and 6 (+ HEIC truncation finding)
**Status**: complete
**Coverage**: 3/3 fixes covered (100%)

## Test Inventory

| Test | File | Criteria | Status |
|------|------|----------|--------|
| Fixture truncated at 1%..99% rejected by both validators, agree | imager/test/HeicTruncationTest.cpp | F1 | ✅ |
| Tail cuts (1/2/16/64/256/1024 B) and tiny heads rejected | HeicTruncationTest.cpp | F1 | ✅ |
| Generated HEICs (sizes, odd size, alpha, lossless, 2x2 and 1x3 grids) accepted by both validators and addFile | HeicTruncationTest.cpp | F1 anti-over-tightening | ✅ |
| Generated HEICs truncated at 8 percent points + tails rejected | HeicTruncationTest.cpp | F1 | ✅ |
| addFile / validateOnlyFile / addImage reject truncated HEIC (BrokenFile), nothing stored | HeicTruncationTest.cpp | F1 | ✅ |
| Existing equivalence test: `truncated-*` HEIC variants must be rejected (tightened) | imager/test/StreamValidatorEquivalenceTest.cpp | F1 | ✅ |
| Duplicate id 64-hex, in-memory (jpg, heic, nef, mov), other filename, addImage | imager/test/DuplicateIdTest.cpp | F2 | ✅ |
| Duplicate id streamed (> 8 MB), repeated | DuplicateIdTest.cpp | F2 | ✅ |
| AAE sidecar duplicate carries id equal to first add | DuplicateIdTest.cpp | F2 | ✅ |
| `server_heic_test`: truncated HEIC → 400 BrokenFile (6 cuts), valid 201, duplicate 200 same id, nothing extra stored (23 checks, port 19680) | server/test/test_heic.sh.in | F1, F2 | ✅ |
| `server_items_test` existing: duplicate 200 with right id | server/test/test_items.sh.in | F2 | ✅ |
| memory-report with interval 60 exits < 5 s (tiny import and empty input); final MEMF line still printed | imagestore/test/test_cli.sh.in | F3 | ✅ |

## Progress Log

- **2026-11-02**: Tests added; clang x2 and g++ x1 full suite green (44/44; `server_heic_test` is new). imagestore_cli_tests now 49 checks. imager_tests 124 cases.

## Known Gaps

- Streamed (> 8 MB) duplicate uses zero-padded valid.heic; no >8 MB truncated-HEIC beyond the existing padded-format test.
- AAE duplicate id equality with the first add is an assumption (passes).
- F3 prompt-exit is timing-based (< 5 s versus 60 s interval), not a pre-fix red/green comparison.
