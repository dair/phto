# Testing: Per-item tags — `/items/{id}/tags`

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.3, §7, §14.2–14.3, §16 H1
**Status**: complete (122 passed / 0 failed)
**Coverage**: 8/8 spec areas covered (100%); 122 assertions

## Test Inventory

Single shell test `server/test/test_item_tags.sh.in` → ctest `server_item_tags_test`
(ports 19530–19534, two-target config, items seeded via `PUT /items`, tags set only through the API).

| Area | Criteria Covered | Status |
|------|------------------|--------|
| GET on untagged item → `{tags:[]}` | GET | ✅ |
| PUT replace (old gone, new present), `[]` clears, duplicate names deduped | PUT | ✅ |
| POST add (`tags` and `tag` forms), duplicate add idempotent | POST | ✅ |
| DELETE one tag; tag not set → 404 | DELETE | ✅ |
| Auto-create on PUT and POST (tag row exists in both DBs) | auto-create | ✅ |
| Cross-checks: `GET /items/{id}`, `?tags=`, `?untagged=true` before/after clear | integration | ✅ |
| Both per-target DBs hold identical `file_tag` + `tag` rows | all-or-nothing | ✅ |
| Errors: 400 malformed/short id, 404 unknown, 401 no token on all 4 routes; bad bodies (malformed JSON, `{}`, `[]`, non-array, non-string, null, empty name) 400 on PUT/POST; bad `tag` form | errors | ✅ |
| Special chars: space and non-ASCII tag names, `%20` / `%C3%A9` on DELETE | special chars | ✅ |

## Progress Log

- **2026-10-31**: Script written from plan, then aligned to h1-dev contract: POST dup → 200 no-op; DELETE tag not set → 404 FileNotFound; PUT rejects `tag`; names >255 bytes or containing comma → 400; validation precedes item lookup. Percent-decoding on DELETE is unverified by h1-dev; test asserts it.

## Known Gaps

- Invalid percent-escapes (%zz, %4, %, a%G1) → 400 and literal `+` are covered.
- No test for tag names containing `/` (routing ambiguity).
