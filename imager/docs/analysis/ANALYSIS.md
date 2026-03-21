# Imager Phase 1 Review (per top-level `CLAUDE.md`)

## Scope
This review compares the current codebase against the Phase 1 requirements in the top-level `CLAUDE.md`. It covers:

- Core facade (`imager/`)
- Config parsing (`config/`)
- Database library (`database/`)
- Validation libraries (`validations/jpeg`, `validations/png`)
- Build system consistency
- Tests (CPPUnit)

## High-level assessment
The core Phase 1 functionality is largely implemented and aligned with the specification: config parsing via toml++, SQLite-backed database with thread safety, JPEG/PNG validation wrappers, SHA256 hashing, multi-root storage, and a facade with the required API. There are a few correctness and spec-compliance gaps that should be addressed, mainly around input validation, directory naming consistency, and subproject CMake version mismatches. Test coverage is strong for the database and acceptable for the facade; config parsing lacks tests.

## Compliance vs. requirements

### Config (TOML)
Status: **Mostly compliant**

- `loadConfig` uses toml++ and enforces required `[storage].roots` and `[database].path`. Good.
- Throws `std::runtime_error` on invalid/missing fields as specified.
- No tests for config parsing (not required explicitly, but a gap for robustness).

### Database library
Status: **Compliant**

- SQLite bundled and built from source with `SQLITE_THREADSAFE=1`.
- Thread safety: `std::shared_mutex`, WAL mode, `busy_timeout=5000`.
- Schema matches spec and foreign keys are enabled.
- Uses prepared statements with bound parameters.
- API covers required CRUD, associations, pagination, and recommended methods.
- Tests include construction, CRUD, pagination, error paths, and multithreading.

### Validation libraries (JPEG/PNG)
Status: **Mostly compliant**

- Expose single functions `validateJpeg` / `validatePng` with `ValidationResult` enum.
- Use libjpeg/libpng bundled sources and link static libs.
- Tests exist and are wired with CTest.
- PNG test follows "no pkg-config" requirement; JPEG test optionally uses pkg-config (not prohibited in JPEG CLAUDE, but inconsistent with PNG guidance).

### Facade (`libimager`)
Status: **Mostly compliant**

- `Imager` exposes required core operations plus additional APIs.
- Implements validation, SHA256 hashing, duplicate detection, and DB insert.
- Multi-root storage is synchronous with rollback on partial write.
- Uses validators through a common `IValidator` interface as specified.

## Gaps and issues

### 1) Null/size validation in `addImage` (correctness)
`Imager::addImage` does not validate `data` for null when `size > 0`. For video extensions (which bypass validation), a null pointer will be passed into the hashing function and can cause undefined behavior. This violates robust handling for malformed inputs.

Impact: potential crash or undefined behavior on invalid caller input.

### 2) Empty storage roots can silently succeed (correctness)
`FileStorage::writeFile` does nothing if `m_roots` is empty. `Imager::addImage` will then insert into the DB without storing any file content. The config loader enforces at least one root, but `Imager` can be constructed directly with an empty config.

Impact: data loss if `Imager` is constructed programmatically without validated config.

### 3) Directory naming mismatch (`validation` vs `validations`) (spec compliance)
Top-level `CLAUDE.md` specifies `validation/` as the directory name. The repo uses `validations/`, and CMake points there. This is a spec mismatch, even though the build works as written.

Impact: documentation drift and potential confusion for contributors.

### 4) CMake minimum versions in validation subprojects (spec compliance)
Top-level CLAUDE expects CMake ≥ 3.28 and references 4.2.3 compatibility. The JPEG and PNG validation subprojects declare lower minimums (`3.15` and `3.14...4.2`).

Impact: not a functional defect, but inconsistent with stated toolchain requirements.

### 5) Hashing is chunked but still requires full data in memory (design mismatch)
The spec stresses streaming file hashing to avoid loading multi-GB files. The current API accepts an in-memory buffer and hashes it in chunks. This is reasonable for the API shape, but it does not satisfy the “never load entire file” intent when the data originates from disk.

Impact: API design does not support streaming from disk; large files must be fully loaded by the caller.

### 6) N+1 query pattern in tag-based queries (performance risk)
`Imager::getImagesByTags` fetches files by tags, then issues `getTagsForFile` per file. This scales poorly for large result sets.

Impact: performance degradation on tag-heavy queries; not explicitly forbidden but worth noting for large libraries.

## Notable alignments and strengths

- Storage sharding uses first two hex chars of SHA256 as required.
- Validator adapters correctly translate `ValidationResult` into `validation::ValidationResult` with error messages.
- Database error mapping for duplicate file insert uses constraint detection and avoids deleting already-written files (acceptable for same-content duplicates).
- Tests cover multi-root storage, concurrency, deduplication, tag workflows, and DB multithreading.

## Overall conclusion
Phase 1 is largely complete and functionally aligned with the requirements in `CLAUDE.md`. The primary correctness risks are around missing input validation in `addImage` and the possibility of silent success with empty storage roots. The main spec-compliance issues are documentation and toolchain inconsistencies (directory naming and CMake minimums), plus a design mismatch with the “streaming hashing” intent.

If these gaps are addressed, the implementation would be solidly compliant with the stated Phase 1 requirements.
