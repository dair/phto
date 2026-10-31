# Testing: Loose ends (L1–L4)

**Plan Reference**: loose-ends step (post-0022)
**Status**: complete

- `server/test/test_loose_ends.sh.in` → `server_loose_ends_test` (122 checks, ports 19600–19609): early/running 413, spool cleanup on every path, bounded memory (290 MB padded MOV, above the streaming threshold (256 MB when written, 8 MB now), upload returns 201, VmHWM ≈ 50 MB, limit 120 MB), TLS huge-Content-Length 413 + close, trusted proxies/XFF, access log format/secrets/off switch, lockout expiry, drain timeout (exit 3), bad config → exit 2.
- `server/test/ClientIpTest.cpp` → `server_clientip_tests` (26 cases): `ClientIpTest` (CIDR, XFF resolution) and `AccessLogFormatTest` (field layout, escaping, truncation).

## Known Gaps
- (Resolved by the streaming step, 2026-10-31: `Imager::addFile` now holds only files ≤ 8 MB in RAM; larger files stream.)
- Query-string stripping lives in `AccessLog::log()`, covered by the shell test, not the unit test.
- Client abort with no response: access-log line not asserted.

## Progress Log
- **2026-10-31**: Tests added; TLS case, MOV memory test and AccessLog::format tests added; suite 37/37 green.
