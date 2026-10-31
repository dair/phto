# Testing: Item content download & delete (`GET /items/{id}/content`, `DELETE /items/{id}`)

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.2, §7, §8, §14.2–14.3, §16 row G3
**Status**: complete
**Coverage**: 8/8 acceptance criteria covered (100%) by 52 assertions

Test: `server/test/test_content.sh.in` → ctest `server_content_test` (ports 19510–19514, 90 s timeout).
Fixtures: `validations/jpeg/test/testimg.jpg` (5770 B), `validations/mov/test/fixtures/valid.mov` (1416 B). `max_upload_mb = 8`.
Alice (non-admin) is used throughout, so DELETE is verified to need no admin rights (plan §6.2 lists none).

## Test Inventory

| Test Name | Criteria Covered | Status |
|-----------|------------------|--------|
| full GET → 200, SHA256 == id, Content-Type `image/jpeg`, Content-Length, `Accept-Ranges: bytes`, ETag `"<id>"` | full download, headers | ✅ |
| `.mov` download: SHA256 matches, Content-Type `video/*` | second format | ✅ |
| `bytes=0-99`, `1000-1099`, `5000-`, `-200` → 206, exact `Content-Range`, body == slice | ranges | ✅ |
| `bytes=<size>-` and `999999-` → 416 + `Content-Range: bytes */size` | unsatisfiable | ✅ |
| `If-None-Match: "<id>"` → 304, empty body; non-matching → 200 full body | ETag | ✅ |
| content: unknown → 404, malformed → 400, no token → 401 | content errors | ✅ |
| DELETE: no token → 401 (file kept), malformed → 400, unknown → 404 | delete errors | ✅ |
| DELETE → 204 empty body, file gone from root, GET + content → 404, second DELETE → 404 | delete success | ✅ |
| other item untouched; deleting it leaves root empty | isolation | ✅ |

## Progress Log

- **2026-10-31**: Script, CMake registration (`MOV_FIXTURE`) and doc written from the plan. Assumptions: `.mov` Content-Type asserted only as `video/*` (exact `video/quicktime` not pinned); 416 and 304 header details per plan.

## Known Gaps

- None blocking; contract confirmed by g3-dev (Cache-Control `private, immutable`, 8 MiB 206 cap, malformed Range ignored → 200 are untested).
- Not covered: multi-range (`bytes=0-1,5-6`), `?format=`/`Accept` transcoding (0023), `Cache-Control: immutable` header, `If-None-Match` with `*`/lists.
- **2026-10-31**: Executed after handover: server_content_test 52/52 pass; full suite passes.
