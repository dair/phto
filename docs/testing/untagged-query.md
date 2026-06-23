# Testing: Untagged Items Query (D1)

**Plan Reference**: `docs/plan/0022.SERVER.md` §8 item 1 (checkpoint D1)
**Status**: complete
**Coverage**: 5/5 acceptance criteria covered (100%)

## Acceptance Criteria (§8 item 1)

1. `db::Database::getUntaggedFiles()` returns files that have no tag bindings.
2. Files with at least one tag binding are excluded.
3. Removing the last tag binding causes the file to appear in subsequent calls.
4. Pagination (`Pagination{offset, limit}`) works correctly; offset past end returns empty.
5. `imager::Imager::getUntaggedImages(offset, limit)` delegates to the DB layer and returns `ImageInfo` objects with an empty `tags` field; tagged images are excluded.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `UntaggedFilesTest::testUntaggedReturnsFilesWithNoTags` | `database/test/DatabaseTest.cpp` | 1 — two untagged files both returned | ✅ |
| `UntaggedFilesTest::testTaggedFileExcluded` | `database/test/DatabaseTest.cpp` | 2 — one tagged, one untagged; only untagged returned | ✅ |
| `UntaggedFilesTest::testUntagAfterRemovalBecomesUntagged` | `database/test/DatabaseTest.cpp` | 3 — unbindTag causes file to appear | ✅ |
| `UntaggedFilesTest::testPagination` | `database/test/DatabaseTest.cpp` | 4 — three files; pages of 2; offset past end | ✅ |
| `UntaggedFilesTest::testEmptyWhenAllTagged` | `database/test/DatabaseTest.cpp` | 1, 2 — result empty when all files tagged | ✅ |
| `UntaggedImagesTest::testReturnsOnlyUntaggedImages` | `imager/test/ImagerTest.cpp` | 5 — facade returns untagged with empty tags field | ✅ |
| `UntaggedImagesTest::testTaggedImageExcluded` | `imager/test/ImagerTest.cpp` | 5 — tagged image absent from result | ✅ |
| `UntaggedImagesTest::testOffsetAndLimit` | `imager/test/ImagerTest.cpp` | 5 — pagination at facade level | ✅ |

CTest suites: `DatabaseTests`, `ImagerTests`.

## Progress Log

- **2026-06-23**: Coverage doc created. No new test cases needed; both layers (DB and facade) are fully covered by the existing test suites.

## Known Gaps

- **Multi-target consistency**: `UntaggedImagesTest` uses a single-target config. A multi-target scenario where files are untagged in one DB but tagged in another (possible only if the all-or-nothing `MultiDatabase` write path has a bug) is not tested. This is an integration-level concern; the existing `MultiTargetTest` suite covers the all-or-nothing write path broadly.
- **Empty-DB base case**: no explicit test for `getUntaggedImages()` on a store with no files at all. The implementation will return an empty vector; this is implicitly correct given the SQL `WHERE id NOT IN (SELECT file_id FROM file_tag)` over an empty `file` table.

## Notes

- `UntaggedImagesTest` uses MOV fixture files (unique `salt1/salt2` bytes to defeat SHA256 deduplication). If the MOV validator fixtures are absent, `addUniqueFile` returns an empty id and each test skips via early return — no false failures.
- The DB-layer tests (`UntaggedFilesTest`) use synthetic string ids (`"f1"`, `"f2"`, `"f3"`) and do not depend on the validator or file-format path.
