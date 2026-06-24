# Testing: Auth Endpoints (F1)

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.1, milestone M-F checkpoint F1
**Status**: complete
**Coverage**: 7/7 acceptance criteria covered (100%)

## Acceptance Criteria (§6.1, F1)

1. `POST /auth/login` with correct `{login, password}` → 200 `{token, expiresAt, login, fullName, isAdmin}`.
2. `POST /auth/login` with wrong password → 401.
3. `POST /auth/login` with malformed/missing field (no password key) → 400.
4. `POST /auth/login` with correct credentials for a disabled account → 403.
5. `GET /auth/me` with a valid Bearer token → 200 `{login, fullName, isAdmin}`.
6. `GET /auth/me` with no Authorization header → 401.
7. `GET /auth/me` with a tampered/invalid token → 401.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `POST /auth/login correct credentials → HTTP 200` | `server/test/test_auth.sh.in` | 1 — success path, status code | ✅ |
| `POST /auth/login response contains token field` | `server/test/test_auth.sh.in` | 1 — token present in body | ✅ |
| `POST /auth/login response isAdmin is false for regular user` | `server/test/test_auth.sh.in` | 1 — isAdmin field correct | ✅ |
| `POST /auth/login response login matches alice` | `server/test/test_auth.sh.in` | 1 — login echo in body | ✅ |
| `GET /auth/me with valid Bearer token → HTTP 200` | `server/test/test_auth.sh.in` | 5 — success path, status code | ✅ |
| `GET /auth/me response login is alice` | `server/test/test_auth.sh.in` | 5 — login field in body | ✅ |
| `GET /auth/me response isAdmin is false` | `server/test/test_auth.sh.in` | 5 — isAdmin field in body | ✅ |
| `GET /auth/me without Authorization header → HTTP 401` | `server/test/test_auth.sh.in` | 6 — missing token rejected | ✅ |
| `GET /auth/me with tampered token → HTTP 401` | `server/test/test_auth.sh.in` | 7 — invalid token rejected | ✅ |
| `POST /auth/login wrong password → HTTP 401` | `server/test/test_auth.sh.in` | 2 — wrong password rejected | ✅ |
| `POST /auth/login missing password field → HTTP 400` | `server/test/test_auth.sh.in` | 3 — malformed body rejected | ✅ |
| `POST /auth/login disabled account correct creds → HTTP 403` | `server/test/test_auth.sh.in` | 4 — disabled user rejected | ✅ |
| `SIGTERM causes clean exit (status 0)` | `server/test/test_auth.sh.in` | (infra) | ✅ |

CTest entry: `server_auth_test` (shell script, 13 assertions, test #22).

### How the test works

1. Picks a likely-free TCP port from 19483–19487 (separate range from `server_health_test`).
2. Creates a `mktemp -d` temp directory; writes a minimal TOML config with a dummy `[[targets]]`, `[server] bind="127.0.0.1"`, and `[auth]` with a 32-byte `jwt_secret` and 3600 s TTL.
3. Seeds user `alice` ("Alice Smith", non-admin) via `imageradmin <config> user add alice "Alice Smith" --password-stdin` (pipes `hunter2` on stdin).
4. Starts `imagerd <config>` in the background, captures its PID.
5. Polls `GET /health` via `curl --retry 10 --retry-delay 1 --retry-connrefused --max-time 12` until the daemon is ready.
6. Runs 7 auth-path assertions (12 individual checks) covering the full §6.1 acceptance matrix.
7. Uses `python3 -c "import json..."` for JSON field extraction (python3 is guaranteed available; avoids a `jq` dependency).
8. Disables alice via `imageradmin <config> user disable alice` before testing the 403 path.
9. Sends `SIGTERM` from the parent shell (not a subshell) so `wait` works; polls up to 5 s for exit; asserts exit status 0.
10. Cleans up temp directory via EXIT trap.
11. Guards with `command -v curl` — skips gracefully if curl is absent.

## Progress Log

- **2026-06-24**: Coverage doc created. `server_auth_test` (CTest #22) written, all 13 assertions pass. Full suite 23/23. All 7 F1 acceptance criteria covered.

## Known Gaps

- **`expiresAt` and `fullName` field validation**: The login response body is checked for `token`, `login`, and `isAdmin`. `expiresAt` and `fullName` are returned by the implementation but not independently asserted in the E2E test. Low risk — the unit-level `auth/test/TokenServiceTest.cpp` covers the token payload, and `fullName` is a pass-through from `UserStore`.
- **Token expiry (time-based)**: `TokenService::verify` rejects expired tokens; this is covered at unit level in `auth/test/TokenServiceTest.cpp` (which tests with a 1 s TTL). Re-testing it here would require a 1 s TTL config + a `sleep` and would add ~2 s to an already sequential test; not added.
- **Admin user flag**: The F1 test only seeds a regular user. The `isAdmin:true` path through `/auth/login` and `/auth/me` is not exercised end-to-end; it is covered by `imageradmin_cli_tests` (user add --admin) and `auth-library` tests at the unit level.
- **`WWW-Authenticate` response header**: §7 specifies this header on 401; `server_json_tests` covers the `unauthorized()` helper, but the E2E test does not assert the header on `/auth/me` 401 responses.
- **Unknown login on POST /auth/login**: The implementation returns 401 for both wrong password and non-existent login (no user enumeration). The test covers wrong password; a completely unknown login is not a separate assertion (same code path, same result).

## Notes

- `configure_file(@ONLY)` cannot evaluate CMake generator expressions. Binary paths are injected via plain CMake variables: `${CMAKE_BINARY_DIR}/server/imagerd` and `${CMAKE_BINARY_DIR}/imageradmin/imageradmin`, mirroring the `imagestore` and `server_health_test` patterns.
- `wait <PID>` in bash only works for child processes of the current shell. The SIGTERM assertion runs in the parent shell, not a `run_test` subshell.
- Cleanup trap guards against `DAEMON_PID=0` to avoid `kill -0 0` accidentally signalling the whole process group.
- CTest timeout is 90 s; actual runtime is ~2 s.
