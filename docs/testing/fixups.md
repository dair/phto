# Testing: Post-0022 fix-ups

**Plan Reference**: `docs/plan/0022.SERVER.md` (fix-up checkpoint after Phase 2)
**Status**: complete
**Coverage**: 7/7 gaps covered (100%) — gaps 1–2 in `imageradmin_cli_tests` (see [imageradmin-cli.md](imageradmin-cli.md)), gaps 3–7 in `server_fixups_test`

Files: `server/test/test_fixups.sh.in` (ctest `server_fixups_test`, ports 19590–19593, timeout 240 s),
`imageradmin/test/test_cli.sh.in`. Results: `server_fixups_test` **88 passed / 0 failed**;
`imageradmin_cli_tests` **75 passed / 0 failed**; full `ctest --preset default` 35/35.

## Test Inventory (server_fixups_test)

| Group | Cases | Gap |
|-------|-------|-----|
| Startup, auth DB in missing dir | plain exit 2 (no crash/timeout/core), quick, `imagerd:` line, "cannot open auth database <path>", dir not created | 3 |
| Startup, auth DB path is a directory | exit 2, `imagerd:` line, "cannot open auth database" | 3 |
| Startup, target DB in missing dir | exit 2, `imagerd:` line, "cannot open storage" | 3 |
| Empty `[auth].database` | exit 2, `imagerd:` line, "Config [auth]: 'database' must not be empty", no DB created | 4 |
| Absent `[auth].database` | exit 2, "no auth database configured" | 4 |
| No / empty-string / empty-file JWT secret | exit 2, "no JWT secret configured (set [auth].jwt_secret_file)" | 5 |
| Short JWT secret | exit 2, "JWT secret must be >= 32 bytes", NOT the "no JWT secret" message | 5 |
| Idle SIGTERM | exits within 10 s, status 0, "shutdown complete" | 6 |
| Drain: slow download | 6 MB item at `--limit-rate 1500k`, SIGTERM mid-transfer (preconditions: still running, partial bytes); new connection refused; curl rc 0, 200, full size, correct sha256; daemon exit 0 in bound; "draining (up to 30s)" logged, no "drain timeout" | 6 |
| Drain: slow upload (documented behaviour) | mid-body upload is cut off (not 201), daemon exits 0 promptly, nothing persisted (item → 404 after restart), new connection refused | 6 |
| Drain: stalled half-sent request | daemon still exits within 30 s + slack, status 0 or 3 | 6 |
| `/auth/password` throttle | first 4 wrong → 401; 5th 401/429; next → 429 + `Retry-After` (positive int) + JSON error body; correct oldPassword during lockout → 429 and password unchanged; other user unaffected; 7 consecutive correct changes → 204 (success not penalised); wrong < threshold then correct → 204; 400 (short newPassword) not counted; counters shared with login (alice login → 429); unauthenticated → 401 | 7 |

## Progress Log

- **2026-10-31**: Written from description, aligned to fx-dev's contract, built and run green.
  Section order: startup → idle → download drain → upload drain → stall → throttle (own daemon, last).

## Known Gaps / Notes

- **Mid-body uploads are not drained.** Crow's `stop()` stops reading new requests; only handlers already
  started finish (static-file download streams). The test asserts this documented behaviour rather than a full
  drain of uploads. Worth a product decision if uploads should drain too.
- Forced exit after the 30 s drain timeout (exit 3, "drain timeout, forcing exit") is not deterministically
  reproducible (would require a handler running > 30 s); the stalled-client case accepts exit 0 or 3.
- Stale-daemon hazard: a previous aborted run can leave an `imagerd` on 19591–19593, causing bind failures.
