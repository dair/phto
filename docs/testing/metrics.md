# Testing: Server request metrics

**Plan Reference**: m4 "server metrics" step (`server/ServerMetrics.{h,cpp}`; no separate plan doc)
**Status**: complete
**Coverage**: 6/6 requirement groups covered (100%) — 87 checks in one script

Test: `server/test/test_metrics.sh.in` → `server_metrics_test` (live `imagerd`, port 19640, ports 19640–19659 reserved).
Python helper embedded in the script (`mx.py`) parses and validates the Prometheus text; `promtool` is used only if installed (not installed here → skipped).

## Test Inventory

| Group | Checks | Criteria Covered | Status |
|-------|--------|------------------|--------|
| Auth/shape | `/metrics` 401 (no token) / 403 (non-admin) / 200 + `text/plain; version=0.0.4`; `/stats` non-admin keys exactly `{imageCount, tagCount}`; admin adds `server` + `library` with documented keys | auth, shape | ✅ |
| Exact counters | 3× GET /items, 1 upload, 1 early 413, 1 unauth 401, 1 unknown login, bob lockout (3× 401 + 1× 429), 1 login success → exact deltas of requests (method×route×class), rejected_total{too_large,unauthorized,throttled}, logins_total, upload bytes == file size, latency count (early 413 not timed), in-flight == 1 when idle, uptime/RSS sanity, `/stats` mirrors | counters | ✅ |
| Cardinality | 30 random ids → one `/items/{id}` series; `/items/{id}/tags`, `/content`, `/users/{login}`, `/tags/{name}`; unknown paths → `other`; all route/method/status_class values from fixed sets; series count bounded | cardinality | ✅ |
| No sensitive data | `/metrics` and `/stats`: no login names, ids (≥40 hex), IPs, tokens/passwords, raw paths/filenames/tag names | privacy | ✅ |
| Format | HELP/TYPE ordering, name & label syntax/escapes, no duplicates, `phto_` prefix, histograms ascending/cumulative, `+Inf` == `_count`, `_sum`; validator self-test rejects bad input; expected families present | format | ✅ |
| Concurrency | 50 parallel GET /health → requests and latency count +50 exactly; 25+25 mixed parallel → +25/+25; in-flight back to 1; histograms still valid | concurrency | ✅ |

## Progress Log

- **2026-10-31**: Script written against m4-dev's contract. First run: 4 test-side failures only (uptime has 1 s resolution; series count bound too tight at 200 — real is ~700 lines of histogram buckets; login name "root" falsely matched `phto_library_storage_write_root_seconds`). Fixed (admin login renamed `opsadmin`, bound 1500, uptime non-decreasing). No product bugs found. Full suite 41/41 on clang ×2 and g++ ×1.

## Known Gaps

- Uptime is only checked as non-decreasing (1-second resolution; a sleep would slow the suite).
- HEAD/OPTIONS/PATCH method labels and 3xx/5xx classes are not provoked (5xx asserted to be absent).
- `promtool` check not exercised (not installed).
- Upload-bytes semantics for uploads rejected after spooling (validation/dedup) not tested.
- TLS and trusted-proxy paths are not combined with metrics.
