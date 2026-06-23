# Testing: Server Config — `[server]` / `[auth]` Sections

**Plan Reference**: `docs/plan/0022.SERVER.md` §11 (Config additions — checkpoint A2)
**Status**: complete
**Coverage**: 10/10 acceptance criteria covered (100%)

## Acceptance Criteria (§11)

The config parser must:

1. Parse `[server].bind`, defaulting to `"0.0.0.0"`.
2. Parse `[server].port` (1–65535), defaulting to `8443`; reject 0 and > 65535.
3. Parse `[server].tls` (bool), defaulting to `false`.
4. Parse `[server].tls_cert` and `[server].tls_key` as paths, defaulting to empty.
5. Parse `[server].threads`, defaulting to `0`.
6. Parse `[server].max_upload_mb` and store as bytes (`× 1024²`); default 4 GiB; reject 0.
7. Parse `[auth].database` path, defaulting to empty.
8. Parse `[auth].jwt_secret` / `[auth].jwt_secret_file`, both defaulting to empty.
9. Parse `[auth].token_ttl_seconds`, defaulting to 43200; reject 0.
10. Parse `[auth].issuer`, defaulting to `"phto"`; parse `[auth].pbkdf2_iterations`, defaulting to 310000; reject < 1000.
11. A config without `[server]`/`[auth]` sections must still load cleanly (backward compat).
12. Partial sections (only some keys present) fill missing keys with defaults.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `ServerAuthDefaultsTest::testNoServerOrAuthSections` | `config/test/ConfigTest.cpp` | 1–10 defaults; criteria 11 — backward compat | ✅ |
| `ServerAuthFullTest::testFullServerAndAuth` | `config/test/ConfigTest.cpp` | 1–10 — all fields parsed from TOML | ✅ |
| `ServerAuthPartialTest::testPartialServer` | `config/test/ConfigTest.cpp` | Criteria 12 — missing `[server]` keys keep defaults; `port` override verified | ✅ |
| `ServerAuthPartialTest::testPartialAuth` | `config/test/ConfigTest.cpp` | Criteria 12 — missing `[auth]` keys keep defaults; `issuer` and `token_ttl_seconds` overrides verified | ✅ |
| `ServerAuthValidationTest::testPortZero` | `config/test/ConfigTest.cpp` | Criteria 2 — port=0 rejected | ✅ |
| `ServerAuthValidationTest::testPortTooLarge` | `config/test/ConfigTest.cpp` | Criteria 2 — port=70000 rejected | ✅ |
| `ServerAuthValidationTest::testPortTypeMismatch` | `config/test/ConfigTest.cpp` | Criteria 2 — port="abc" rejected | ✅ |
| `ServerAuthValidationTest::testMaxUploadMbZero` | `config/test/ConfigTest.cpp` | Criteria 6 — max_upload_mb=0 rejected | ✅ |
| `ServerAuthValidationTest::testTokenTtlZero` | `config/test/ConfigTest.cpp` | Criteria 9 — token_ttl_seconds=0 rejected | ✅ |
| `ServerAuthValidationTest::testPbkdf2IterationsTooLow` | `config/test/ConfigTest.cpp` | Criteria 10 — pbkdf2_iterations=10 (< 1000) rejected | ✅ |

## Progress Log

- **2026-06-23**: Coverage doc created. No test gaps found; all 10 criteria covered by the 10 test cases in `ServerAuthDefaultsTest`, `ServerAuthFullTest`, `ServerAuthPartialTest`, and `ServerAuthValidationTest`.

## Known Gaps

- **`[auth].jwt_secret_file` presence check**: the spec (§13) says the daemon refuses to start if `jwt_secret_file` is world-readable or too short. These are boot-time validations inside `imagerd/main.cpp` (not yet implemented), not config parsing. When the server milestone lands, a corresponding test for `Config` would need to check whether that validation lives in the parser or the daemon startup; currently out of scope.
- **`[server].threads` type mismatch**: a non-integer `threads` value is parsed leniently (no explicit validation in the parser); a type-mismatch test could be added later for completeness.

## Notes

- The CTest suite is `config_tests` (one binary for all `ConfigTest.cpp` classes).
- `max_upload_mb` conversion: the parser stores `mb × 1024 × 1024` as `uint64_t` in `server.maxUploadBytes`; `testFullServerAndAuth` explicitly verifies the byte-conversion arithmetic for `max_upload_mb = 2048`.
- Existing `[[targets]]`-only configs must still parse without `[server]`/`[auth]` — verified by `testNoServerOrAuthSections`, which also double-checks every single default value.
