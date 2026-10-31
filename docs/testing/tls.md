# Testing: Native TLS (imagerd)

**Plan Reference**: `docs/plan/0022.SERVER.md` §10, §11, §13, §16 row J2
**Status**: complete — 29 assertions, 0 failed
**Coverage**: 7/7 spec behaviours covered (100%)

Test: `server/test/test_tls.sh.in` → ctest `server_tls_test` (ports 19570–19574:
19570 TLS daemon, 19571 plain daemon, 19572 startup-failure cases, 19574
TLS ≤1.1 probe). A self-signed cert/key is generated at test time with the
`openssl` CLI; the test exits 77 (ctest SKIP) if `openssl` or `curl` is missing.

## Test Inventory

| Check | Criteria Covered | Status |
|-------|------------------|--------|
| HTTPS `/health` with `--cacert` → 200 | native TLS serves | ✅ |
| HTTPS login → token → `/auth/me` | full auth flow over TLS | ✅ |
| No `--cacert` / wrong CA → curl exit 60 | real handshake, cert verified | ✅ |
| `--insecure` → 200 | handshake OK, only trust differs | ✅ |
| Plain HTTP on TLS port ≠ 200; HTTPS still works after | no plaintext fallback | ✅ |
| TLS 1.1-only client rejected (skipped w/ NOTE if local curl/OpenSSL cannot do 1.1); 1.2 and 1.3 accepted | minimum protocol | ✅ |
| Startup fails fast (non-zero, not timeout/hang, stderr non-empty) for: missing cert, missing key, mismatched key, garbage cert PEM, garbage key PEM, key mode 0644 | fail-fast validation | ✅ |
| `tls = false`: plain HTTP 200, HTTPS fails, SIGTERM exit 0 | plain mode unchanged | ✅ |
| SIGTERM on TLS daemon → exit 0 | graceful shutdown | ✅ |

## Progress Log

- **2026-10-31**: Test written; aligned to j2-dev contract; 29 passed / 0 failed. Gotcha: `wait` must not run inside `$(...)` (SIGTERM check uses a global result var).

## Known Gaps

- Contract (j2-dev): startup failure → exit 2, stderr `imagerd: TLS error: <msg>`; key with S_IROTH set rejected, group-readable allowed (group-readable and 0604 variants not tested); min TLS 1.2.
- Unset tls_cert/tls_key config case not tested.
