# Testing: Auth Library — PasswordHash, UserStore, TokenService

**Plan Reference**: `docs/plan/0022.SERVER.md` §5.1–5.3 (checkpoints B1–B3)
**Status**: complete
**Coverage**: 21/21 acceptance criteria covered (100%)

---

## Section 1: PasswordHash (`auth/test/PasswordHashTest.cpp`)

### Acceptance Criteria (§5.2)

1. `hashPassword` returns `algo = "pbkdf2-sha256"`, 16-byte salt, 32-byte hash, correct iteration count.
2. Correct password verifies against its stored record.
3. Wrong password does not verify.
4. Two calls with the same password produce different salts and different hashes (random salt).
5. Both records from criterion 4 still verify correctly against their own password.
6. Tampering any byte of the stored hash causes verify to fail.
7. Changing the stored iteration count causes verify to fail.
8. Unsupported `algo` string causes verify to return `false` (not throw).
9. Empty password is accepted by PBKDF2 and round-trips correctly. **(added 2026-06-23)**

### Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `PasswordHashTest::testOutputShape` | `auth/test/PasswordHashTest.cpp` | 1 — algo, salt/hash size, iterations | ✅ |
| `PasswordHashTest::testCorrectPasswordVerifies` | `auth/test/PasswordHashTest.cpp` | 2 | ✅ |
| `PasswordHashTest::testWrongPasswordFails` | `auth/test/PasswordHashTest.cpp` | 3 | ✅ |
| `PasswordHashTest::testSaltRandomness` | `auth/test/PasswordHashTest.cpp` | 4, 5 | ✅ |
| `PasswordHashTest::testTamperHashFails` | `auth/test/PasswordHashTest.cpp` | 6 | ✅ |
| `PasswordHashTest::testWrongIterationsFails` | `auth/test/PasswordHashTest.cpp` | 7 | ✅ |
| `PasswordHashTest::testUnsupportedAlgoFails` | `auth/test/PasswordHashTest.cpp` | 8 — returns false, no throw | ✅ |
| `PasswordHashTest::testEmptyPasswordVerifies` | `auth/test/PasswordHashTest.cpp` | 9 — empty password round-trip | ✅ |

CTest suite: `auth_tests` (8 tests).

---

## Section 2: UserStore (`auth/test/UserStoreTest.cpp`)

### Acceptance Criteria (§5.1)

10. `create` + `get` returns a user with correct fields; non-existent login returns `nullopt`.
11. `getPassword` round-trips `PasswordRecord` bytes exactly; absent login returns `nullopt`.
12. `create` with a duplicate login throws `AuthException(Duplicate)`.
13. `remove` deletes the user; a subsequent `get` returns `nullopt`.
14. `remove` on a missing login throws `AuthException(NotFound)`.
15. `setEnabled` toggles the `enabled` flag; missing login throws `AuthException(NotFound)`.
16. `setAdmin` toggles the `isAdmin` flag; missing login throws `AuthException(NotFound)`.
17. `setPassword` replaces stored credentials; missing login throws `AuthException(NotFound)`.
18. `list()` returns users ordered by login; pagination (offset/limit) works; offset past end returns empty.
19. `count()` reflects inserts and removes.
20. Concurrent multi-thread read/write does not corrupt data or deadlock.

### Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `UserStoreTest::testCreateAndGet` | `auth/test/UserStoreTest.cpp` | 10 | ✅ |
| `UserStoreTest::testGetPasswordRoundTrip` | `auth/test/UserStoreTest.cpp` | 11 | ✅ |
| `UserStoreTest::testDuplicateThrows` | `auth/test/UserStoreTest.cpp` | 12 | ✅ |
| `UserStoreTest::testRemove` | `auth/test/UserStoreTest.cpp` | 13 | ✅ |
| `UserStoreTest::testRemoveMissingThrows` | `auth/test/UserStoreTest.cpp` | 14 | ✅ |
| `UserStoreTest::testSetEnabled` | `auth/test/UserStoreTest.cpp` | 15 — toggle true/false/true | ✅ |
| `UserStoreTest::testSetEnabledMissingThrows` | `auth/test/UserStoreTest.cpp` | 15 — NotFound path | ✅ |
| `UserStoreTest::testSetAdmin` | `auth/test/UserStoreTest.cpp` | 16 — toggle false/true/false | ✅ |
| `UserStoreTest::testSetAdminMissingThrows` | `auth/test/UserStoreTest.cpp` | 16 — NotFound path | ✅ |
| `UserStoreTest::testSetPassword` | `auth/test/UserStoreTest.cpp` | 17 — new password verifies; old does not | ✅ |
| `UserStoreTest::testSetPasswordMissingThrows` | `auth/test/UserStoreTest.cpp` | 17 — NotFound path | ✅ |
| `UserStoreTest::testListOrdering` | `auth/test/UserStoreTest.cpp` | 18 — alphabetical order | ✅ |
| `UserStoreTest::testListPagination` | `auth/test/UserStoreTest.cpp` | 18 — offset/limit paging | ✅ |
| `UserStoreTest::testListOffsetBeyondEnd` | `auth/test/UserStoreTest.cpp` | 18 — offset past end → empty | ✅ |
| `UserStoreTest::testCount` | `auth/test/UserStoreTest.cpp` | 19 | ✅ |
| `UserStoreTest::testConcurrency` | `auth/test/UserStoreTest.cpp` | 20 — 8 threads × 50 ops; 0 errors; count intact | ✅ |

CTest suite: `auth_userstore_tests` (16 tests).

---

## Section 3: TokenService (`auth/test/TokenServiceTest.cpp`)

### Acceptance Criteria (§5.3)

21. Issue + verify round-trip for a regular user: claims `login`, `fullName`, `isAdmin=false` parsed correctly.
22. Issue + verify round-trip for an admin user: `isAdmin=true` parsed correctly.
23. An expired token (exp in the past) is rejected by `verify` (returns `nullopt`).
24. A token signed with a different secret is rejected.
25. A tampered token (payload byte flipped) is rejected.
26. A token with a wrong `iss` claim is rejected.
27. Garbage / empty / malformed input to `verify` returns `nullopt` (never throws).
28. Two tokens issued for the same user carry different `jti` values (per-token randomness). **(added 2026-06-23)**

### Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| `TokenServiceTest::testRoundTripRegularUser` | `auth/test/TokenServiceTest.cpp` | 21 | ✅ |
| `TokenServiceTest::testRoundTripAdminUser` | `auth/test/TokenServiceTest.cpp` | 22 | ✅ |
| `TokenServiceTest::testExpiredTokenRejected` | `auth/test/TokenServiceTest.cpp` | 23 — minted with `exp = now − 10s` | ✅ |
| `TokenServiceTest::testWrongSecretRejected` | `auth/test/TokenServiceTest.cpp` | 24 | ✅ |
| `TokenServiceTest::testTamperedTokenRejected` | `auth/test/TokenServiceTest.cpp` | 25 | ✅ |
| `TokenServiceTest::testWrongIssuerRejected` | `auth/test/TokenServiceTest.cpp` | 26 | ✅ |
| `TokenServiceTest::testGarbageInputRejected` | `auth/test/TokenServiceTest.cpp` | 27 — three inputs: short, empty, garbage chars | ✅ |
| `TokenServiceTest::testJtiUniquenessAcrossIssues` | `auth/test/TokenServiceTest.cpp` | 28 — two tokens for same user must differ | ✅ |

CTest suite: `auth_token_tests` (8 tests).

---

## Progress Log

- **2026-06-23**: Coverage doc created. Added two targeted tests:
  - `PasswordHashTest::testEmptyPasswordVerifies` — empty password is a valid PBKDF2 input; no prior test covered this path.
  - `TokenServiceTest::testJtiUniquenessAcrossIssues` — spec §5.3 mandates a random `jti` per token; uniqueness was not previously asserted.
  Both land in the existing `auth_tests` / `auth_token_tests` suites. All 20 ctest entries remain green.

## Known Gaps

- **`updatedAt` changes on `setPassword`/`setEnabled`/`setAdmin`**: the spec schema has an `updated_at` column. No test verifies it is bumped on mutation operations (only that `createdAt == updatedAt` at creation). Low-priority addition.
- **Disabled user `getPassword` path**: no test checks that `getPassword` returns the stored record even when `enabled=false` (the caller — login handler — is responsible for checking `enabled` after verifying the password). Noted for when server handler tests land.
- **Short secret rejection**: the spec (§13) says the daemon refuses to start if the JWT secret is < 32 bytes. This is a boot-time check in `imagerd/main.cpp`, not in `TokenService` itself (which accepts any secret). No `TokenService` unit test is warranted; the validation belongs to the server startup coverage.
- **`jti` structural verification**: `testJtiUniquenessAcrossIssues` verifies token-level uniqueness as a proxy; it does not decode the JWT to inspect the raw `jti` claim. A future test could parse the payload segment with a minimal base64 decode to assert the claim is a non-empty hex string.
