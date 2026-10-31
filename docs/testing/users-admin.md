# Testing: Admin user management — `/users` routes

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.5, §7, §13, §16 row I1
**Status**: complete (118 assertions passed, 0 failed; full ctest 30/30)
**Coverage**: 8/8 acceptance criteria covered (100%)

Test file: `server/test/test_users.sh.in` → ctest `server_users_test` (ports 19550–19554, timeout 120 s).
Contract knobs (guard status, PATCH unknown-field status, stateless-token status) are variables at the top of the script.

## Test Inventory

| Group | Cases | Criteria |
|-------|-------|----------|
| Bootstrap (imageradmin root + bob, logins) | 2 | setup |
| Create: regular + admin → 201, both log in, admin reaches /users, regular 403 | 6 | POST 201 |
| Duplicate → 409 (and no overwrite) | 2 | POST 409 |
| Bad bodies → 400 (missing login/password, `{}`, non-JSON, wrong types ×4, short password, spaced/empty login) + nothing created ×5 | 17 | POST 400 |
| Read: fields, no hash/salt/password keys (single + list), list works, unknown → 404 | 10 | GET |
| Authz: non-admin 403 ×6, anonymous 401 ×5, garbage token 401, no side effects ×2 | 14 | authz |
| PATCH: fullName, password, isAdmin promote/demote, enabled false/true (login refused, existing token), multi-field | ~26 | PATCH 204 |
| PATCH bad: `{}`, non-JSON, wrong types ×3, unknown field, unknown login 404 | 7 | PATCH 400/404 |
| Delete: 204, GET 404, login fails, token status, second DELETE 404, absent from list | 6 | DELETE |
| Percent-encoded login (`enc%5Fuser`) GET/PATCH/DELETE | 6 | encoding |
| Last-admin guard: sole admin DELETE/demote/disable rejected, state unchanged, two admins permit demote/delete, disabled admin not counted, deleting disabled admin allowed | ~18 | guard |
| SIGTERM clean exit | 1 | shutdown |

## Progress Log

- **2026-10-31**: Script, CMake registration and docs written from the plan. Not yet run.

## Known Gaps / Assumptions

- Contract (i1-dev): guard `409 LastAdmin`; unknown PATCH key `400`; disabled/deleted user token → `401` (middleware re-reads DB); demoted token → `403` immediately; login charset `[A-Za-z0-9._@-]` (encoding test uses `enc%40user`).
- Missing `fullName` / `isAdmin` on POST not asserted (spec ambiguity).
- **2026-10-31**: Aligned to i1-dev contract (token revocation 401, @ in login, malformed escape 400, self-delete).
- **2026-10-31 (final)**: Ran after handover: server_users_test 118 passed / 0 failed; full ctest 30/30.
