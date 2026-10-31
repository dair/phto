# Testing: Hardening (J1) — headers, /stats, validation, body cap, throttle

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.6, §7, §13, §16 row J1
**Status**: complete (249 assertions passed, 0 failed; full ctest 31/31)
**Coverage**: 8/8 acceptance criteria covered (100%)

Test file: `server/test/test_hardening.sh.in` → ctest `server_hardening_test` (ports 19560–19564, timeout 180 s).
Two daemons: the main one (sections 1–6) and a fresh one for the per-IP throttle (section 7, run last so the lockout affects nothing else).
Contract knobs (body cap 65536, per-IP limit 20, per-login limit 5, code names, `SECRET_CHECKS`) are variables at the top of the script.

## Test Inventory

| Group | Criteria |
|-------|----------|
| Headers: nosniff on every route family (health, login, items list/get/upload, content, tags, item tags, users, stats) and on 400/401/403/404/409/413 + unknown route; `no-store` on JSON; content `private, immutable` and not `no-store` | headers |
| `/stats`: exact keys, integer counts; correct after uploads, duplicate upload, MOV upload, item delete, tag create/delete; 401 without/invalid token; admin allowed | /stats |
| Validation: NUL / 0x1F / 0x7F / newline / tab (raw and `\u` escaped), invalid UTF-8 (0xFF, truncated, overlong, surrogate) in JSON bodies for POST /tags, PUT and POST /items/{id}/tags; same via percent-encoding in `DELETE /tags/…`, `DELETE /items/{id}/tags/…`, `GET /items?tags=` (single and in a list). DB1/DB2 row counts unchanged; valid UTF-8 still accepted | validation |
| Pagination: `limit` 501/0/-1/huge and `offset` -1 → 400 and `limit=500` → 200 on /items, /tags, /users | bounded pagination |
| Body cap: cap+1, cap+1024, 8×cap → 413 `TooLarge` on tags/item tags/login/users; just-under cap processed (400 for long name, 200 for a large valid tag list); no tag created; `/health` and authed requests fine afterwards | body cap |
| Error codes: NotFound (item, tag, user), Conflict (tag, user), Unauthorized, BadRequest (pagination, bad id), Forbidden, UnsupportedFormat 415 | vocabulary |
| Secret file: missing, 0644 and 31-byte-or-shorter secret → imagerd exits non-zero; 0600 adequate secret starts | secret file |
| Per-IP throttle: 19 distinct-login failures → 401; 429 + `Retry-After` + `TooManyRequests` + nosniff; correct passwords refused during lockout; `/health` still reachable | per-IP throttle |
| SIGTERM → clean exit on both daemons | shutdown |

## Progress Log

- **2026-10-31**: Script written from the plan, aligned with j1-dev's contract. 249 passed / 0 failed; full ctest 31/31.

## Known Gaps

- Lockout expiry (900 s) is not tested.
- No check on how the throttle treats IPs other than 127.0.0.1 (e.g. `X-Forwarded-For`).
- `PUT /items` is exempt from the body cap; this is covered by the existing upload tests, not here.
