# Testing: Full API end-to-end journey (J3)

**Plan Reference**: `docs/plan/0022.SERVER.md` §14.3, §16 row J3
**Status**: complete (69 assertions passed, 0 failed; full ctest 33/33)
**Coverage**: 9/9 journey steps covered (100%)

Test file: `server/test/test_api.sh.in` → ctest `server_api_test` (ports 19580–19584, timeout 120 s).
One daemon, two targets (separate roots and DBs), plain HTTP. A single client journey; per-area edge cases live in the other `test_*.sh.in` files.

## Test Inventory (journey steps)

| Step | What is asserted |
|------|------------------|
| 1 Bootstrap | `imageradmin` seeds admin; `GET /health` 200 + nosniff |
| 2 Admin session | login, token, `no-store` on JSON, `/auth/me`, `POST /users` alice → 201 |
| 3 Uploads | user login; JPEG ×2, generated PNG, MOV → 201, id = sha256, duplicate → 200 `duplicate:true`, files in both roots |
| 4 Tagging | `POST /tags` ×3, `PUT`/`POST /items/{id}/tags`, `?tags=` AND, `?untagged=true`, pagination covers 4 distinct items, `/stats` = 4 images / 3 tags |
| 5 Retrieval | item metadata, content sha256 = id (JPEG/PNG/MOV), Content-Type, Range 0-99 → 206, `If-None-Match` → 304 |
| 6 Authz | user 403 `Forbidden` on `/users`; no token 401 `Unauthorized`; admin PATCH `enabled:false` → user's existing token 401 without re-login, re-login 403, admin unaffected |
| 7 Cleanup | `DELETE /tags/gamma` unbinds; item delete → content 404 `NotFound`, file gone from both roots; stats 3/2; delete user; `DELETE /users/root` → 409 `LastAdmin`; `file`/`tag`/`file_tag` identical across DBs (3/2/3 rows); 3 files per root |
| 8 Headers | nosniff on health, upload, list, 409; `no-store` on login, list, 409 |
| 9 Shutdown | SIGTERM → exit 0 |

## Progress Log

- **2026-10-31**: Written, 69 passed / 0 failed. No server bugs found. Full ctest 33/33.

## Known Gaps

- "Demote" is exercised via `enabled:false` (the stronger token-invalidation case); demote-by-`isAdmin:false` is covered in `users-admin.md`.
- PNG is generated in-script (no PNG fixture file in the repo).
- Plain HTTP only; TLS and throttle are in `tls.md` / `hardening.md`.
