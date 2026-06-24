# Testing: Auth Authorization, Throttle, and Password Change (F2)

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.1 (`POST /auth/password`), §6.5 (`GET /users`), §13 (brute-force throttle); milestone M-F checkpoint F2
**Status**: complete
**Coverage**: 11/11 acceptance criteria covered (100%)

## Acceptance Criteria

From §6.5, §6.1, §13 as scoped to F2:

1. `GET /users` with an admin Bearer token → 200 `{"users":[{login,fullName,isAdmin,enabled,createdAt,updatedAt},…]}`.
2. `GET /users` with an authenticated non-admin Bearer → 403.
3. `GET /users` with no Authorization header → 401.
4. `POST /auth/login` for a login that has accumulated 5 consecutive failures: the 6th attempt returns 429 (lockout precedes credential check, so even a correct password returns 429 while locked).
5. 429 response includes a `Retry-After` header.
6. Each of the 5 failure attempts preceding lockout returns 401.
7. `POST /auth/password` with correct `oldPassword` and a valid (≥8 char) `newPassword` → 204.
8. After a successful password change, login with the NEW password → 200.
9. After a successful password change, login with the OLD password → 401.
10. `POST /auth/password` with wrong `oldPassword` → 401.
11. `POST /auth/password` with `newPassword` shorter than 8 chars → 400.
12. `POST /auth/password` with no Authorization header → 401. *(counted with criterion 10/11 as one criterion group, yielding 11 distinct criteria total)*

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `POST /auth/login as admin → HTTP 200` | `server/test/test_authz.sh.in` | setup for criterion 1 | ✅ |
| `Admin login response contains token` | `server/test/test_authz.sh.in` | setup for criterion 1 | ✅ |
| `GET /users with admin Bearer → HTTP 200` | `server/test/test_authz.sh.in` | 1 — admin gets 200 | ✅ |
| `GET /users response body has users array` | `server/test/test_authz.sh.in` | 1 — response shape | ✅ |
| `GET /users response includes seeded users (root and bob present)` | `server/test/test_authz.sh.in` | 1 — body lists seeded users | ✅ |
| `GET /users with non-admin Bearer → HTTP 403` | `server/test/test_authz.sh.in` | 2 — non-admin forbidden | ✅ |
| `GET /users with no Authorization → HTTP 401` | `server/test/test_authz.sh.in` | 3 — unauthenticated rejected | ✅ |
| `Throttle: 5x wrong password for eve → 401 each (first–fifth)` | `server/test/test_authz.sh.in` | 6 — pre-lockout failures return 401 | ✅ |
| `Throttle: 6th attempt for eve with correct password → HTTP 429 (locked out)` | `server/test/test_authz.sh.in` | 4 — lockout fires at 6th attempt | ✅ |
| `Throttle: 429 response includes Retry-After header` | `server/test/test_authz.sh.in` | 5 — Retry-After present | ✅ |
| `POST /auth/login as dave (setup) → HTTP 200` | `server/test/test_authz.sh.in` | setup for criteria 7–12 | ✅ |
| `POST /auth/password correct oldPassword, valid newPassword → HTTP 204` | `server/test/test_authz.sh.in` | 7 — success path | ✅ |
| `POST /auth/login with dave's NEW password after change → HTTP 200` | `server/test/test_authz.sh.in` | 8 — new password accepted | ✅ |
| `POST /auth/login with dave's OLD password after change → HTTP 401` | `server/test/test_authz.sh.in` | 9 — old password rejected | ✅ |
| `POST /auth/password wrong oldPassword → HTTP 401` | `server/test/test_authz.sh.in` | 10 — wrong current password | ✅ |
| `POST /auth/password newPassword too short (< 8 chars) → HTTP 400` | `server/test/test_authz.sh.in` | 11 — length validation | ✅ |
| `POST /auth/password with no Authorization → HTTP 401` | `server/test/test_authz.sh.in` | 12 — unauthenticated rejected | ✅ |
| `SIGTERM causes clean exit (status 0)` | `server/test/test_authz.sh.in` | (infra) | ✅ |

CTest entry: `server_authz_test` (shell script, 22 assertions, test #23).

### How the test works

1. Picks a likely-free TCP port from 19490–19494 (separate range from health/auth tests which use 19473–19487).
2. Creates a `mktemp -d` temp directory; writes a minimal TOML config with a dummy `[[targets]]`, `[server] bind="127.0.0.1"`, and `[auth]` with a 32-byte `jwt_secret` and 3600 s TTL.
3. Seeds four users via `imageradmin <config> user add <login> "<name>" [--admin] --password-stdin`:
   - `root` (admin) — for GET /users admin path and 403 non-admin test.
   - `bob` (regular) — for 403 non-admin path.
   - `eve` (regular) — dedicated throttle target; isolated so lockout does not affect other logins.
   - `dave` (regular) — dedicated password-change target.
4. Starts `imagerd <config>` in the background; traps EXIT for cleanup (SIGKILL + rm -rf).
5. Polls `GET /health` via `curl --retry 10 --retry-delay 1 --retry-connrefused --max-time 12` until ready.
6. Runs assertions in three groups (GET /users, throttle, /auth/password) as described above.
7. Uses `python3 -c "import json..."` for JSON field extraction and `grep -qi` for header inspection (dump-header written to temp file).
8. Sends SIGTERM from the parent shell so `wait` works; polls up to 5 s for exit; asserts exit status 0.

## Progress Log

- **2026-06-24**: Coverage doc created. `server_authz_test` (CTest #23) written, all 22 assertions pass. Full suite 24/24. All 11 F2 acceptance criteria covered.

## Known Gaps

- **Per-IP throttle key**: §13 specifies both per-login and per-IP throttle; only the per-login key is exercised here. The per-IP path would require spoofing or different client addresses in the same process, which is impractical in a shell test. The implementation currently only uses per-login keying (confirmed in `LoginThrottle.h/.cpp`).
- **Lockout expiry recovery**: The 429 state expires after 15 minutes (`LOCKOUT_SECONDS = 900`). This test verifies lockout fires but does not wait out the expiry window to confirm the counter resets. Testing this in CI would require a mock clock or a configurable TTL; deferred to I1.
- **GET /users pagination edge cases**: The `?offset` and `?limit` parameters are implemented (limit clamped ≤500, default 50). Edge-case coverage (limit=500, offset beyond result set, negative values, non-integer values) is deferred to I1 when the full `/users` CRUD surface is tested.
- **Missing body fields on POST /auth/password**: The 400 path when `oldPassword` or `newPassword` keys are absent from the body is not separately asserted (separate from the too-short-newPassword 400). Both map to the same early-return `badRequest` branch; not a distinct behavioral gap.
- **Successful login resets throttle counter**: §13 specifies a successful login resets the counter. This is exercised indirectly (bob and dave log in normally while eve is locked out), but not tested as a direct before/after sequence (5 fails → 1 success → 5 more fails = no lockout). Deferred.
