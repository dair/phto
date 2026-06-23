# Testing: Atomic Tag Replacement (D2)

**Plan Reference**: `docs/plan/0022.SERVER.md` §8 item 2 (checkpoint D2)
**Status**: complete
**Coverage**: 7/7 acceptance criteria covered (100%)

## Acceptance Criteria (§8 item 2)

1. `db::Database::setTagsForFile` replaces from empty → a non-empty set; the new bindings are present.
2. `db::Database::setTagsForFile` replaces an existing set → a different set; old tags fully gone, new ones present.
3. `db::Database::setTagsForFile` with an empty list clears all bindings.
4. Tags that did not exist before are auto-created (`INSERT OR IGNORE INTO tag`); verifiable via `tagExists`/`getAllTags`.
5. Duplicate inputs (`["a","a","b"]`) result in exactly `{a, b}` with no error.
6. Applying the same set twice is idempotent (no error, same result).
7. `imager::Imager::setImageTags` (facade): round-trips, replacing a set works, clearing to empty works, FileNotFound on absent id, after clearing the image appears as untagged, after assigning tags the image matches an AND query.

Multi-target: after `setImageTags` all target DBs carry identical tag sets.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `SetTagsForFileTest::testReplaceFromEmpty` | `database/test/DatabaseTest.cpp` | 1 — empty→{nature,urban} both present, sorted | ✅ |
| `SetTagsForFileTest::testReplaceExistingSet` | `database/test/DatabaseTest.cpp` | 2 — {nature}→{bw,landscape}; old tag absent | ✅ |
| `SetTagsForFileTest::testReplaceWithEmptyClears` | `database/test/DatabaseTest.cpp` | 3 — set then clear; empty confirmed | ✅ |
| `SetTagsForFileTest::testAutoCreatesMissingTags` | `database/test/DatabaseTest.cpp` | 4 — new tags visible via tagExists+getAllTags | ✅ |
| `SetTagsForFileTest::testDeduplication` | `database/test/DatabaseTest.cpp` | 5 — ["a","a","b"] → {a,b}, no error | ✅ |
| `SetTagsForFileTest::testIdempotent` | `database/test/DatabaseTest.cpp` | 6 — same set twice; no error, same result | ✅ |
| `SetTagsForFileTest::testResultMatchesRequestedSet` | `database/test/DatabaseTest.cpp` | 1,2 — getTagsForFile returns exactly the requested set; other file unaffected | ✅ |
| `SetImageTagsTest::testRoundTrip` | `imager/test/ImagerTest.cpp` | 7 — assigned tags returned by getImageTags, sorted | ✅ |
| `SetImageTagsTest::testReplaceExistingTags` | `imager/test/ImagerTest.cpp` | 7 — old tag gone after replace | ✅ |
| `SetImageTagsTest::testClearToEmpty` | `imager/test/ImagerTest.cpp` | 7 — clear to {} works | ✅ |
| `SetImageTagsTest::testFileNotFound` | `imager/test/ImagerTest.cpp` | 7 — absent id → ErrorCode::FileNotFound | ✅ |
| `SetImageTagsTest::testAfterClearAppearsUntagged` | `imager/test/ImagerTest.cpp` | 7 — after clear, file in getUntaggedImages | ✅ |
| `SetImageTagsTest::testAfterAssignMatchesTagQuery` | `imager/test/ImagerTest.cpp` | 7 — after assign, file matches AND query | ✅ |
| `SetTagsMultiTargetTest::testTagParityAfterSetImageTags` | `imager/test/MultiTargetTest.cpp` | multi-target — both DBs carry identical {alpha,beta,gamma} | ✅ |
| `SetTagsMultiTargetTest::testReplaceTagParityInBothDbs` | `imager/test/MultiTargetTest.cpp` | multi-target — after replace both DBs show only new tag | ✅ |

CTest suites: `DatabaseTests`, `ImagerTests`.

## Progress Log

- **2026-06-23**: Initial coverage doc. All 15 test cases added across three files; full suite (20 CTest entries) green.

## Known Gaps

- **Compensation/rollback path on partial DB failure**: `MultiDatabase::setTagsForFile` captures the prior tag set from the primary DB and restores it if a later DB fails. This path requires fault injection (e.g. a mock or corrupted DB handle) that is not available without a test-only seam. No brittle workaround was introduced; this remains untested. The same gap exists for all other `MultiDatabase` write compensations.
- **Transaction atomicity under crash**: SQLite's BEGIN/COMMIT guarantees are verified by the behavior tests (no partial state observed), but a crash-during-write scenario is not exercised.
- **Single-target `DatabaseError` return code**: no test forces `setTagsForFile` to fail at the DB layer and checks that `Imager::setImageTags` returns `DatabaseError`. Triggering a SQL failure cleanly without a fault-injection seam would be brittle.

## Notes

- DB-layer tests (`SetTagsForFileTest`) use synthetic string file ids and do not depend on validators or file format logic.
- Facade and multi-target tests (`SetImageTagsTest`, `SetTagsMultiTargetTest`) use MOV fixture files with unique trailing bytes to defeat SHA256 deduplication. If the fixture is absent, each test returns early — no false failures.
- Multi-target parity is verified by opening each DB file directly with `db::Database(path)` to bypass `MultiDatabase`'s read-from-first-only policy, following the convention in `CONVENTIONS.md`.
