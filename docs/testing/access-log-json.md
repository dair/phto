# Testing: Structured (JSON) access log + X-Request-Id

**Plan Reference**: none (small step; spec = `[server].access_log_format` and request-id rules from the l5 brief; see `server/AccessLog.h`)
**Status**: complete
**Coverage**: all brief criteria covered (see Test Inventory) (135 shell checks + 15 CPPUnit cases)

## Test Inventory

| Test | File | Criteria covered |
|------|------|------------------|
| `AccessLogJsonTest` (15 cases) | `server/test/ClientIpTest.cpp` | field order/layout, login/bytes null, single line, `"` `\` control-char escaping, newline-forgery, invalid/valid UTF-8, escaping in every string field, duration is a number, `parseFormat`, `validRequestId` boundaries and hostile values, 1000 unique generated ids |
| `server_access_log_json_test` (ctest; ports 19660–19666) | `server/test/test_access_log_json.sh.in` | see below |

Shell test sections: (1) JSON daemon: schema validation of every line (exact key set, types, RFC 3339 ms `ts`), login null/alice, route templates (`/items/{id}`, `other`), query never logged, secrets absent, request-id echo/validation/replacement (boundaries 1 and 64, 65, space, `;`, quote, JSON-forge, `%0d%0a`, control byte, empty, duplicate headers), header on 200/401/404/413/429, 200 unique ids matching logged ids, hostile percent-encoded and raw-byte request lines (invalid UTF-8), 50 concurrent requests. (2) `text` and default daemons: old 8-field format, no JSON, header still present. (3) `access_log = false` silences both formats. (4) 429 via login throttle. (5) bad `access_log_format` ("xml", "", 42, "jsonl") exits 2 with `imagerd:` line.

## Progress Log

- **2026-10-31**: Tests written and first run. Test-side fixes: parser drops connections for control bytes in headers (accepted), 404 bodies are empty so text bytes is `-`, JSON may escape as `\u000a`/`\u0009`. Found product issues (below), reported to l5-dev.

- **2026-10-31**: Spec decision: `bytes` always numeric (0 included). Product bug fixed by l5-dev: empty `client_ip` on unmatched routes (Crow patch crow-remote-ip.patch). Final: full suite 42/42 on clang x2 and g++ x1.

## Known Gaps

- On unmatched routes Crow completes the request before headers are parsed: inbound `X-Request-Id`, XFF and `duration_ms` are not asserted there.
- Control bytes inside a header value are rejected by Crow's HTTP parser (connection closed, no response), so replacement is untestable for them.
- `duration_ms` value accuracy is not asserted (only type, ≥ 0, < 10 s).
