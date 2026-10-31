# Testing: Global tags — `GET/POST /tags`, `DELETE /tags/{name}`

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.4, §7, §14, §16 H2
**Status**: complete (104 passed / 0 failed)
**Coverage**: 9/9 spec areas covered (100%); 104 assertions

## Test Inventory

Single shell test `server/test/test_tags.sh.in` → ctest `server_tags_test`
(ports 19540–19544, two-target config, items seeded via `PUT /items`, tags set only through
the API; DBs are read with python3 sqlite3 to verify).

| Area | Criteria Covered | Status |
|------|------------------|--------|
| Empty list → 200 `{tags:[]}`; created tag appears; tag rows in both DBs | GET/POST | ✅ |
| Duplicate POST → 409 `DuplicateFile`; single entry remains; 409 for H1-created / bound tags | POST | ✅ |
| Order by name; pagination walk with no gaps/dups; offset==total, past end, limit 1/500, default 50 | GET paging | ✅ |
| Bad paging (`-1`, `0`, `501`, `abc`, `1.5`, empty, overflow, `5x`) → 400 BadRequest | GET paging | ✅ |
| Tags auto-created via H1 PUT/POST appear in `GET /tags` | integration | ✅ |
| DELETE unbinding: 204 empty body; items' tags, `?tags=x`, `?untagged=true`, both DBs (`tag` + `file_tag`) | DELETE | ✅ |
| Re-create after delete; delete unbound tag; unknown → 404; repeat → 404 | DELETE | ✅ |
| Percent-decoding (`%20`, `%C3%A9`, literal `+`); invalid escapes → 400 | DELETE | ✅ |
| POST validation: malformed/empty/non-object bodies, wrong types, empty name, comma, 256 bytes → 400; 255 bytes and 2-byte `é` → 201 | POST | ✅ |
| 401 (no/invalid token) on all 3 routes; no side effects; non-admin may create/delete | auth | ✅ |
| Both DBs identical at the end; SIGTERM clean exit | multi-target | ✅ |

## Progress Log

- **2026-10-31**: Script written from plan §6.4 and team-lead defaults: sorted by name, 409 code `DuplicateFile`, create/delete not admin-only. h2-dev contract not yet received; element shape (string vs object) is tolerated by the `names` helper.

- **2026-10-31 (final)**: h2-dev contract confirmed (bare-string elements, sorted, 409 DuplicateFile, 404 FileNotFound, not admin-only, POST 201 `{"name"}`). First run had a duplicated-upload bug in the test (fixed). server_tags_test 104 passed / 0 failed; full ctest 29/29.

## Known Gaps

- 403 for non-admins is not tested because create/delete are not admin-only (confirmed by h2-dev).
- Response envelope echo (offset/limit) for `GET /tags` is not asserted (unspecified).
- (none beyond unspecified envelope echo)
