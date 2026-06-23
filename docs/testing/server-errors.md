# Testing: Server Error Utilities — `server/Json.{h,cpp}`

**Plan Reference**: `docs/plan/0022.SERVER.md` §7 / checkpoint E2
**Status**: complete
**Coverage**: 7/7 acceptance criteria covered (100%)

## Acceptance Criteria (§7 / E2)

1. `httpStatusFor` returns the correct HTTP status for every `imager::ErrorCode` value.
2. `errorCodeName` returns the exact enum spelling for every `imager::ErrorCode` value.
3. `jsonError(status, code, message)` returns a `crow::response` with `.code == status`, `Content-Type: application/json`, and body parses to `{"error":{"code":"...","message":"..."}}` with matching values — including messages containing special characters.
4. `errorResponse(ErrorCode, msg)` derives status and envelope code from the single-sourced table; message passes through unchanged.
5. `badRequest(msg)` → 400, envelope code `"BadRequest"`.
6. `unauthorized(msg)` → 401, envelope code `"Unauthorized"`, header `WWW-Authenticate: Bearer`.
7. `forbidden(msg)` → 403, envelope code `"Forbidden"`.

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `HttpStatusForTest::testOk` | `server/test/JsonTest.cpp` | C1 — Ok → 200 | ✅ |
| `HttpStatusForTest::testBrokenFile` | `server/test/JsonTest.cpp` | C1 — BrokenFile → 422 | ✅ |
| `HttpStatusForTest::testDuplicateFile` | `server/test/JsonTest.cpp` | C1 — DuplicateFile → 409 | ✅ |
| `HttpStatusForTest::testUnsupportedFormat` | `server/test/JsonTest.cpp` | C1 — UnsupportedFormat → 415 | ✅ |
| `HttpStatusForTest::testFileNotFound` | `server/test/JsonTest.cpp` | C1 — FileNotFound → 404 | ✅ |
| `HttpStatusForTest::testStorageError` | `server/test/JsonTest.cpp` | C1 — StorageError → 500 | ✅ |
| `HttpStatusForTest::testAmbiguousSidecar` | `server/test/JsonTest.cpp` | C1 — AmbiguousSidecar → 409 | ✅ |
| `HttpStatusForTest::testDatabaseError` | `server/test/JsonTest.cpp` | C1 — DatabaseError → 500 | ✅ |
| `HttpStatusForTest::testConfigError` | `server/test/JsonTest.cpp` | C1 — ConfigError → 500 | ✅ |
| `HttpStatusForTest::testTooLarge` | `server/test/JsonTest.cpp` | C1 — TooLarge → 413 | ✅ |
| `ErrorCodeNameTest::testOk` | `server/test/JsonTest.cpp` | C2 — "Ok" | ✅ |
| `ErrorCodeNameTest::testBrokenFile` | `server/test/JsonTest.cpp` | C2 — "BrokenFile" | ✅ |
| `ErrorCodeNameTest::testDuplicateFile` | `server/test/JsonTest.cpp` | C2 — "DuplicateFile" | ✅ |
| `ErrorCodeNameTest::testUnsupportedFormat` | `server/test/JsonTest.cpp` | C2 — "UnsupportedFormat" | ✅ |
| `ErrorCodeNameTest::testFileNotFound` | `server/test/JsonTest.cpp` | C2 — "FileNotFound" | ✅ |
| `ErrorCodeNameTest::testStorageError` | `server/test/JsonTest.cpp` | C2 — "StorageError" | ✅ |
| `ErrorCodeNameTest::testAmbiguousSidecar` | `server/test/JsonTest.cpp` | C2 — "AmbiguousSidecar" | ✅ |
| `ErrorCodeNameTest::testDatabaseError` | `server/test/JsonTest.cpp` | C2 — "DatabaseError" | ✅ |
| `ErrorCodeNameTest::testConfigError` | `server/test/JsonTest.cpp` | C2 — "ConfigError" | ✅ |
| `ErrorCodeNameTest::testTooLarge` | `server/test/JsonTest.cpp` | C2 — "TooLarge" | ✅ |
| `JsonErrorTest::testStatusCode` | `server/test/JsonTest.cpp` | C3 — `.code` matches given status | ✅ |
| `JsonErrorTest::testContentTypeHeader` | `server/test/JsonTest.cpp` | C3 — Content-Type: application/json | ✅ |
| `JsonErrorTest::testEnvelopeCode` | `server/test/JsonTest.cpp` | C3 — body["error"]["code"] matches | ✅ |
| `JsonErrorTest::testEnvelopeMessage` | `server/test/JsonTest.cpp` | C3 — body["error"]["message"] matches | ✅ |
| `JsonErrorTest::testMessageWithSpecialChars` | `server/test/JsonTest.cpp` | C3 — special-char message round-trips | ✅ |
| `ErrorResponseTest::testBrokenFileStatus` | `server/test/JsonTest.cpp` | C4 — BrokenFile → 422 | ✅ |
| `ErrorResponseTest::testBrokenFileEnvelopeCode` | `server/test/JsonTest.cpp` | C4 — BrokenFile envelope code | ✅ |
| `ErrorResponseTest::testFileNotFoundStatus` | `server/test/JsonTest.cpp` | C4 — FileNotFound → 404 | ✅ |
| `ErrorResponseTest::testFileNotFoundEnvelopeCode` | `server/test/JsonTest.cpp` | C4 — FileNotFound envelope code | ✅ |
| `ErrorResponseTest::testDatabaseErrorStatus` | `server/test/JsonTest.cpp` | C4 — DatabaseError → 500 | ✅ |
| `ErrorResponseTest::testDatabaseErrorEnvelopeCode` | `server/test/JsonTest.cpp` | C4 — DatabaseError envelope code | ✅ |
| `ErrorResponseTest::testMessagePassthrough` | `server/test/JsonTest.cpp` | C4 — message passes through | ✅ |
| `TransportHelpersTest::testBadRequestStatus` | `server/test/JsonTest.cpp` | C5 — badRequest → 400 | ✅ |
| `TransportHelpersTest::testBadRequestEnvelopeCode` | `server/test/JsonTest.cpp` | C5 — badRequest code "BadRequest" | ✅ |
| `TransportHelpersTest::testUnauthorizedStatus` | `server/test/JsonTest.cpp` | C6 — unauthorized → 401 | ✅ |
| `TransportHelpersTest::testUnauthorizedEnvelopeCode` | `server/test/JsonTest.cpp` | C6 — unauthorized code "Unauthorized" | ✅ |
| `TransportHelpersTest::testUnauthorizedWWWAuthenticateHeader` | `server/test/JsonTest.cpp` | C6 — WWW-Authenticate: Bearer | ✅ |
| `TransportHelpersTest::testForbiddenStatus` | `server/test/JsonTest.cpp` | C7 — forbidden → 403 | ✅ |
| `TransportHelpersTest::testForbiddenEnvelopeCode` | `server/test/JsonTest.cpp` | C7 — forbidden code "Forbidden" | ✅ |

Total: 39 test cases across 5 fixtures.

## Progress Log

- **2026-06-23**: Coverage doc created. All 7 acceptance criteria covered by 39 test cases in `server_json_tests` (CTest entry #22).

## Known Gaps

None. All enum values, all functions, all edge cases (special-char escaping, WWW-Authenticate header) are covered.

## Notes

- The CTest entry is named `server_json_tests` (matches the executable name); registered in `server/test/CMakeLists.txt` alongside the existing `server_health_test` shell-script test.
- JSON parsing in tests uses `crow::json::load(res.body)` directly — no extra dependencies, consistent with the existing crow dependency already in `server_lib`.
- The special-character test uses `R"(file "foo\bar" rejected)"` — a raw string with an embedded double quote and backslash — to verify crow's JSON serialiser escapes and `load()` un-escapes symmetrically.
- `errorResponse` spot-checks cover BrokenFile (422), FileNotFound (404), and DatabaseError (500); the exhaustive per-value coverage is already supplied by `HttpStatusForTest` and `ErrorCodeNameTest`.
