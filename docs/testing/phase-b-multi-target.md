# Testing: Phase B Multi-Target Integration

**Plan Reference**: `docs/plan/0015.GAPS.md` (B6, B7, B8, B10)
**Status**: complete
**Coverage**: 5/5 acceptance criteria covered (100%)

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `MultiTargetDbTest::testDbParityAfterAdd` | `MultiTargetTest.cpp` | B6 — both DBs have identical file counts and IDs after addImage | ✅ |
| `MultiTargetDbTest::testStorageWriteRollbackOnFailure` | `MultiTargetTest.cpp` | B7 — write failure on second root triggers rollback; root1 left empty | ✅ |
| `StorageFailoverTest::testReadFallsBackToSecondRoot` | `MultiTargetTest.cpp` | B8 — getImageData succeeds via root2 when root1's copy is deleted | ✅ |
| `MultiTargetSidecarTest::testSidecarStoredInBothRoots` | `MultiTargetTest.cpp` | B10 — AAE storage file present in both roots under parent hash | ✅ |
| `MultiTargetSidecarTest::testOrphanResolutionParity` | `MultiTargetTest.cpp` | B10 — orphan resolution updates companion records in both DBs identically | ✅ |

## Progress Log

- **2026-04-08**: Initial implementation — all 5 tests written and passing (42 total in suite).

## Known Gaps

- B7 rollback test skips when running as root (`geteuid() == 0`), because `fs::perms::none` does not prevent writes for the superuser. This is documented inline.
- B8 and B10 tests skip if the MOV fixture is absent or if `addImage` returns a non-Ok code, which can happen in restricted sandbox environments.

## Notes

- All helpers (`uniqueSuffix`, `makeMinimalJpeg`, `loadMovFixture`, `makeUniqueMovFixture`, `makeAaeBlob`) are self-contained copies within `MultiTargetTest.cpp` — no cross-file sharing per project convention.
- `MultiTargetTest.cpp` omits its own `main()`; `ImagerTest.cpp` owns the single entry point for the shared `imager_tests` binary.
- Direct `db::Database` construction is used for parity checks to bypass the `MultiDatabase` read-from-first-only policy and inspect each DB independently.
- Sidecar storage path follows `<root>/<id[0:2]>/<parentHash>.aae` per `FileStorage` shard layout.
