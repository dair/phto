# Testing: imagerd Daemon — E1 (Health endpoint + graceful shutdown)

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.6, §12, milestone M-E checkpoint E1
**Status**: in-progress
**Coverage**: 3/5 acceptance criteria covered (60%)

## Acceptance Criteria (§6.6, §12, E1)

1. `GET /health` returns HTTP 200.
2. `GET /health` response body is `{"status":"ok"}` (Content-Type: application/json, no DB hit).
3. Daemon accepts `SIGTERM` and exits cleanly (status 0); logs `imagerd: shutdown complete`.
4. Daemon accepts `SIGINT` and exits cleanly (status 0).
5. JWT secret validated at startup — daemon refuses to start if secret is missing or shorter than 32 bytes.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `GET /health returns HTTP 200` | `server/test/test_health.sh.in` | 1 — liveness check returns 200 | ✅ |
| `GET /health body contains {"status":"ok"}` | `server/test/test_health.sh.in` | 2 — correct JSON body | ✅ |
| `SIGTERM causes clean exit (status 0)` | `server/test/test_health.sh.in` | 3 — graceful SIGTERM shutdown | ✅ |

CTest entry: `server_health_test` (shell script, 3 assertions).

### How the test works

1. Picks a likely-free TCP port (tries 19473–19477).
2. Creates a `mktemp -d` temp directory; writes a valid TOML config there with a dummy `[[targets]]`, `[server] bind="127.0.0.1"`, and `[auth]` with a 32-byte `jwt_secret`.
3. Starts `imagerd <tmpconfig>` in the background, captures its PID.
4. Polls `GET http://127.0.0.1:<port>/health` via `curl --retry 10 --retry-delay 1 --retry-connrefused --max-time 12` until the daemon is ready.
5. Asserts HTTP status == 200 (assertion 1) and body contains `"status"` and `"ok"` (assertion 2).
6. Sends `SIGTERM` to the daemon from the **parent shell** (not a subshell) so `wait` works; polls up to 5 s for the process to exit; asserts exit status is 0 (assertion 3); SIGKILLs and fails if it doesn't exit in time.
7. Cleans up the temp directory via EXIT trap.
8. Guards with `command -v curl` — skips gracefully if curl is absent.

## Progress Log

- **2026-06-23**: Coverage doc created. `server_health_test` (CTest #21) written and verified passing. 3 of 5 E1 acceptance criteria covered. Gaps noted below.

## Known Gaps

- **SIGINT path (criterion 4)**: The implementation handles SIGINT identically to SIGTERM (both call `handleSignal` → `app.stop()`). Not separately tested because a shell test cannot cleanly send SIGINT without also interrupting the script's own signal handling. Low risk given the identical handler path; could be added with a `trap INT` workaround.
- **JWT secret validation (criterion 5)**: `main.cpp` exits 2 if the secret is shorter than 32 bytes, but no test covers this startup-rejection path. Could be added as a separate shell test assertion (start daemon with short secret, assert it exits non-zero quickly).
- **Native TLS (J2)**: TLS is a later checkpoint; no test coverage planned at E1.
- **`GET /stats` endpoint (§6.6)**: Requires authentication (Bearer JWT). Not part of E1 scope; deferred to auth endpoint tests.
- **`shutdown complete` log message**: The exit-0 assertion verifies graceful shutdown; the exact stderr log line is not captured/asserted (could be added with daemon stderr redirect + grep).
- **Thread count from config**: `[server].threads` is wired through but not load-tested here.

## Notes

- `configure_file(@ONLY)` cannot evaluate CMake generator expressions. The binary path is injected via a plain CMake variable `${CMAKE_BINARY_DIR}/server/imagerd`, mirroring the `imagestore` pattern.
- `wait <PID>` in bash only works for child processes of the current shell. The SIGTERM assertion intentionally runs in the parent shell (not a `run_test` subshell) to make `wait` work correctly.
- Cleanup trap guards against `DAEMON_PID=0` to avoid `kill -0 0` accidentally matching the whole process group.
- CTest timeout is 60 s; actual runtime is ~1–2 s on a development machine.
