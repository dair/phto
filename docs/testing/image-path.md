# Testing: Image Path Accessor (D3)

**Plan Reference**: `docs/plan/0022.SERVER.md` §8 item 3 (checkpoint D3)
**Status**: complete
**Coverage**: 5/5 acceptance criteria covered (100%)

## Acceptance Criteria (§8 item 3)

1. `FileStorage::resolveStoredPath(id, ext)` returns the path of the first
   available root using the same sharding logic as `readFile`
   (`<root>/<id[0:2]>/<id>.<ext>`).
2. `FileStorage::resolveStoredPath` returns `std::nullopt` when no root has
   the file on disk.
3. `Imager::getImagePath(id)` returns `std::nullopt` for an unknown id (DB
   lookup returns nullopt → early return).
4. `Imager::getImagePath(id)` returns a present, existing path after a
   successful `addImage`/`addFile`; the bytes at that path equal those
   returned by `getImageData(id)` (accessor consistency).
5. For sidecar files (AAE), `getImagePath` resolves via the companion
   `storageId` (parent hash prefix) so the returned path matches the actual
   on-disk location.
6. (Multi-root) With 2 roots, `getImagePath` returns a path under the **first**
   root when both copies exist; after removing the first root's copy it returns
   the **second** root's path.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `GetImagePathTest::testPathExistsAfterAdd` | `imager/test/ImagerTest.cpp` | 1, 4 — path present and exists on disk after add | ✅ |
| `GetImagePathTest::testPathConsistentWithData` | `imager/test/ImagerTest.cpp` | 4 — disk bytes equal getImageData bytes | ✅ |
| `GetImagePathTest::testUnknownIdReturnsNullopt` | `imager/test/ImagerTest.cpp` | 3 — unknown id → nullopt | ✅ |
| `GetImagePathTest::testFileDeletedFromDiskReturnsNullopt` | `imager/test/ImagerTest.cpp` | 2 — file in DB but absent on disk → nullopt | ✅ |
| `GetImagePathTest::testSidecarPathUsesParentHash` | `imager/test/ImagerTest.cpp` | 5 — AAE path uses parent hash as filename prefix | ✅ |
| `GetImagePathMultiRootTest::testPathFromFirstRoot` | `imager/test/MultiTargetTest.cpp` | 6a — path under root1 when both present | ✅ |
| `GetImagePathMultiRootTest::testPathFallsBackToSecondRoot` | `imager/test/MultiTargetTest.cpp` | 6b — falls back to root2 after root1 copy removed | ✅ |

CTest suite: `ImagerTests` (both `.cpp` files compile into one binary; `ImagerTest.cpp` owns `main()`).

## Progress Log

- **2026-06-23**: Coverage doc created. 7 tests added across `GetImagePathTest`
  (single-root, 5 cases) and `GetImagePathMultiRootTest` (multi-root, 2 cases)
  in the existing `ImagerTests` binary. No new test binary created.

## Known Gaps

- **Orphan-then-parent sidecar**: `testSidecarPathUsesParentHash` only covers
  the "parent first, then AAE" (Scenario A) path. The orphan-then-parent
  relocation path (Scenario B: AAE added before parent, then parent added) is
  exercised by `SidecarTest::testAddParentResolvesOrphan` via `getImageData`
  but not yet via `getImagePath`. Low risk: `getImagePath` and `getImageData`
  share identical companion-resolution code; the relocation mechanics are
  tested by the existing sidecar suite.
- **Three-root fallthrough**: only 2-root fallthrough is tested. The logic in
  `resolveStoredPath` is a simple sequential loop; additional roots would
  behave identically.

## Notes

- Tests that depend on the MOV fixture (`loadMovFixture`) skip gracefully when
  the fixture is absent rather than fail.
- `testFileDeletedFromDiskReturnsNullopt` deletes the file with
  `std::filesystem::remove` while leaving the DB record intact, directly
  exercising the "no root has it" branch of `resolveStoredPath`.
- The sidecar test uses `path->stem()` to verify that the filename stem equals
  the parent's SHA256 hash — this is the most direct way to assert the
  `storageId` indirection without reaching into `FileStorage` internals.
