# Testing: Items upload & fetch (`PUT /items`, `GET /items/{id}`)

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.2, §7, §9, §11, §14.3, §16 row G1
**Status**: complete
**Coverage**: 9/9 acceptance criteria covered (100%)

Test: `server/test/test_items.sh.in` → ctest `server_items_test` (ports 19500–19504, 90 s timeout).
Fixture: `validations/jpeg/test/testimg.jpg` (no new binaries). Config sets `[server] max_upload_mb = 1`.

## Test Inventory

| Test Name | Criteria Covered | Status |
|-----------|------------------|--------|
| PUT new JPEG → 201, `duplicate:false`, id == SHA256 | upload new | ✅ |
| stored at `<root>/<aa>/<hash>.jpg`, byte-identical | sharded storage | ✅ |
| PUT same bytes (via `?filename=`) → 200, `duplicate:true`, same id | duplicate | ✅ |
| `.txt` upload → 415 `UnsupportedFormat` | unsupported | ✅ |
| random bytes named `.jpg` → 400 `BrokenFile` | broken | ✅ |
| 2 MB body with 1 MB cap → 413 + JSON envelope | oversized | ✅ |
| rejected uploads leave no stored files | no side effects | ✅ |
| PUT no token / invalid token → 401; GET no token → 401 | auth | ✅ |
| GET existing → 200 with id, name, ext, size, tags | get existing | ✅ |
| GET unknown 64-hex id → 404 envelope | get 404 | ✅ |
| GET malformed / short id → 400 `BadRequest` | get 400 | ✅ |
| SIGTERM clean exit | shutdown | ✅ |

## Progress Log

- **2026-10-31**: Script, CMake registration and doc written from the plan. JSON field names for `GET /items/{id}` (`id,name,size,ext,tags` mirroring `ImageInfo`) and `duplicate`/`id` for PUT are assumed pending g1-dev's contract.

## Known Gaps

- Tags are seeded by writing to the per-target SQLite DB with python3's `sqlite3` (tag routes arrive in milestone H).
- Not covered: multi-root storage, `X-Filename` missing/empty (400?), temp-spool file cleanup, AAE sidecar uploads, uploads at the exact cap boundary.
- Ambiguity: whether `GET /items/{id}` 404 vs 400 for uppercase hex; tested only lowercase.

## Decisions


## Progress Log (cont.)

- **2026-10-31**: Green against g1-dev's implementation: 31/31 assertions; full suite 25/25.
