# Testing: Streaming ingest (bounded memory)

**Plan Reference**: streaming step (post-0022; builds on `docs/plan/0017.LARGE_FILES.md`)
**Status**: complete
**Coverage**: 4/4 requirements covered (100%) — equivalence, library memory, server memory, imagestore dry-run/AAE

`STREAMING_THRESHOLD` = 8 MB: files ≤ 8 MB are read into a `Blob`, larger files stream in ≤ 4 MB chunks. `.aae` > 8 MB → `TooLarge`.

## Test Inventory

| Test | File | Covers | Status |
|------|------|--------|--------|
| `StreamingEquivalenceTest` (18 cases, in `ImagerTests`) | `imager/test/StreamingEquivalenceTest.cpp` | addFile / validateOnlyFile / hashOnlyFile / deleteFile == Blob API (code + id) for all valid fixtures (jpeg, heic, nef, mov, mp4-ext, aae, generated png; golden SHA256s) and broken variants (truncated, header-corrupt, garbage, zeros, empty, 1-byte); wrong/unknown/upper-case extension; duplicate + stored-bytes identity; deleteFile id; AAE size limits; AAE > 8 MB TooLarge; padded MOV/JPEG at ±1 byte around 64K…64M incl. 8 MB and 12 MB; 256 MiB±1 MOV; stage callbacks (in-memory vs streamed); missing file; AAE sidecar pairing/orphan relocation/cascade via file paths | ✅ |
| `ImagerMemoryTests` (64 MB and 200 MB MOV) | `imager/test/StreamingMemoryTest.cpp` | own process; VmHWM growth < 48 MB across validateOnlyFile/hashOnlyFile/addFile/duplicate/deleteFile | ✅ |
| `server_streaming_mem_test` (21 checks, port 19620) | `server/test/test_streaming_mem.sh.in` | imagerd VmHWM < baseline+40 MB for 64 MB and 200 MB uploads, download identical, duplicate → 200 same id, JPEG golden id, broken → 4xx, spool clean | ✅ |
| `imagestore_dryrun_streaming_tests` (26 checks) | `imagestore/test/test_dryrun_streaming.sh.in` | dry-run summary/exit codes/OK-DUP-ERR/no writes unchanged; peak RSS (getrusage) < baseline+40 MB for dry-run and import of 64/200 MB | ✅ |
| `StreamValidatorEquivalenceTest` (9 cases, in `ImagerTests`; incl. Adam7-interlaced PNG small + >8 MB accepted by Blob and stream validators and addFile, truncated interlaced rejected) | `imager/test/StreamValidatorEquivalenceTest.cpp` | per format (jpg, png, heic, nef, mov, aae): `IStreamValidator::validateFile` vs `IValidator::validate` agree on valid fixture + ~40 broken variants each (truncations, flipped/zeroed bytes, scrambled, trailing zeros, garbage, zeros); missing file; every format padded to 9 MB via Imager vs Blob API; generated 2000x2000 noise PNG (> 8 MB) accepted by addFile/validateOnlyFile, truncated copies rejected, corrupted copies agree | ✅ (found+fixed: HEIC stream validator rejected valid.heic; NEF file-vs-buffer seek semantics accepted corrupt IFD0 offset) |
| `LargeFileTest` | `imager/test/LargeFileTest.cpp` | constants moved to 8 MB (+1 MB / +512 KB) | ✅ |

## Measurements (clang, 2026-10-31)
- Library, 200 MB MOV: baseline VmHWM 30 MB; after validate +15, hash +16, add +25, dup +29, delete +33 MB.
- imagestore: baseline 38 MB; dry-run 64 MB → 47 MB, 200 MB → 47 MB; import 50 MB.
- imagerd: baseline 37 MB; after 64 MB upload 50 MB, after 200 MB 60 MB, after duplicate 61 MB.

## Progress Log
- **2026-10-31**: Suites added; full suite 40/40 on clang (x2) and g++ after HEIC and NEF stream-validator fixes.

## Known Gaps
- Streamed-path oracle comparison to the Blob API only up to 64 MiB (larger files compare file ops with each other).
- **Fixed (fx2/F1):** both HEIC validators now decode with `strict_decoding = 1`; truncated HEICs are rejected on the Blob and stream paths (asserted in `StreamValidatorEquivalenceTest`, `HeicTruncationTest`). Previously libheif accepted a half-truncated file.
- **Fixed bugs found by these tests:** (1) `HeicStreamValidator` rejected valid HEICs (`heifWaitForFileSize` never reported beyond-EOF); (2) `NefStreamValidator` (`libraw_open_file`) accepted a NEF with corrupt IFD0 offset that the Blob validator rejects — now uses a clamped datastream.
- Fixed (fx2/F2): `addFile`/`addImage` now return the existing id with `DuplicateFile` (`DuplicateIdTest`). Pre-existing behaviour documented, not changed: stage reporting differs between in-memory (Validating) and streamed (Hashing) paths.
- No concurrency stress (many files in flight) memory test.
- StreamingEquivalenceTest.cpp exceeds 300 lines (could be split).
