# Imager Development Analysis

## Scope

This document compares the current repository state against the plan documents referenced from [`CLAUDE.md`](../../CLAUDE.md):

- `docs/plan/README.md`
- `docs/plan/0001.INITIAL.md`
- `docs/plan/0002.DATABASES.md`
- `docs/plan/0003.PARALLELIZING.md`
- `docs/plan/0004.REFACTORING.md`
- `docs/plan/0005.MONITORING.md`
- `docs/plan/0006.UTILITY.md`
- `database/CLAUDE.md`

The analysis is based on the project-owned code under `blob/`, `config/`, `coro/`, `database/`, `imager/`, `metrics/`, and `validations/`. Bundled third-party trees under `database/sqlite/`, `validations/jpeg/libjpeg/`, and `validations/png/libpng/` were treated as dependencies rather than implementation targets.

## Current high-level status

The implementation is materially ahead of the original Phase 1 baseline:

- `0001` is implemented in substance, though the public API and configuration format have evolved beyond the original draft.
- `0002` is largely implemented: per-target databases, coroutine fan-out, and all-or-nothing write semantics exist.
- `0003` is substantially implemented for the hot paths: `Blob`, parallel validate+hash, parallel storage fan-out, and parallel tag enrichment are all present.
- `0004` is largely implemented: `Blob` and coroutine support were extracted to top-level libraries, and `Types.h` was split into individual type headers.
- `0005` is partially implemented: metrics infrastructure exists and is wired into major ingestion/storage paths, but the planned coverage is incomplete.
- `0006` is not implemented: there is no `imagestore/` utility and no dry-run/validate-only facade API.

The repository also contains some cleanup debt:

- documentation drift remains in a few places;
- there is no test coverage for `config/`, `blob/`, `coro/`, or `metrics/`;
- the old internal coroutine headers still exist under `imager/src/coro/` even though the active implementation now lives in top-level `coro/`.

## Plan correspondence

### `docs/plan/README.md`

Status: **Partially accurate**

Still accurate:

- the project is an offline-first C++23 library for media ingestion and organization;
- multi-root file redundancy is implemented;
- SQLite-backed metadata is implemented;
- config is loaded at startup only;
- Phase 2 HTTP/JWT work is still absent.

Drift from the current code:

- the README still says file identity is SHA-256 plus file size; the actual dedup key used by `Imager::addImage` is only the SHA-256 hex string, while size is stored separately in the database;
- it still discusses `validation/`, but the repository uses `validations/`;
- it lists implementation status only up to `0003`, while `0004` and part of `0005` are already in the codebase.

### `0001.INITIAL.md`

Status: **Implemented in spirit, but superseded**

Implemented:

- top-level CMake builds the main subprojects with C++23;
- bundled SQLite, libjpeg, and libpng are used rather than system versions;
- `config/` exists and is a separate library;
- `database/` exists and provides the expected SQLite wrapper API;
- `imager/` provides the facade;
- JPEG and PNG validation wrappers exist;
- MP4 and MOV are accepted by extension only;
- duplicate detection, storage, tagging, listing, lookup, and deletion are implemented;
- tests exist for database, validators, and facade behavior.

Superseded or changed:

- the original config shape (`[storage]` and `[database]`) has been replaced by `[[targets]]`;
- the public ingestion API changed from raw pointer + size to `Blob`;
- the original single-database design has been replaced by `MultiDatabase`;
- the project now has top-level `blob/`, `coro/`, and `metrics/` libraries, which were not part of the original baseline.

Notable drift:

- the sample config at [`imager/sample/config.toml.sample`](../../imager/sample/config.toml.sample) still uses the obsolete `[storage]` / `[database]` format, so the example does not match the actual parser;
- `0001` describes file identity as hash plus size, but the effective lookup key is hash only;
- ingestion is still memory-buffer based end-to-end; hashing is chunked internally, but there is no true streaming ingestion path from caller to storage/database.

### `0002.DATABASES.md`

Status: **Largely implemented**

Implemented:

- `config::AppConfig` is now `std::vector<TargetConfig>` in [`config/include/config/Config.h`](../../config/include/config/Config.h);
- `config::loadConfig` requires non-empty `[[targets]]` and validates `root` and `database` string fields in [`config/src/Config.cpp`](../../config/src/Config.cpp);
- `MultiDatabase` exists in [`imager/src/MultiDatabase.h`](../../imager/src/MultiDatabase.h) and [`imager/src/MultiDatabase.cpp`](../../imager/src/MultiDatabase.cpp);
- one `db::Database` is opened per target;
- write operations are fanned out in parallel on a shared thread pool;
- successful writes are compensated on failure for `addFile`, `deleteFile`, `editFileName`, `addTag`, `deleteTag`, `bindTag`, and `unbindTag`;
- reads intentionally come from the first database only;
- a custom coroutine layer exists and is used as the execution mechanism.

Good alignment details:

- the design intent of “all databases kept identical” is clearly reflected in `MultiDatabase::parallelWriteAll`;
- `Imager::Impl` owns one shared thread pool and passes it to both `MultiDatabase` and `FileStorage`, which matches the architecture direction in the plan.

Gaps and risks:

- there is no dedicated test coverage for multi-database consistency across multiple targets; the current imager tests verify multi-root file copies but do not verify that all target databases stay in sync;
- rollback paths are not fault-injected or directly tested;
- `MultiDatabase` assumes `m_dbs[0]` exists on reads; that is safe through the config parser, but direct construction with an empty target list would still be invalid;
- config validation remains structural only: duplicate roots, duplicate database paths, and other semantic issues are not checked.

### `0003.PARALLELIZING.md`

Status: **Substantially implemented**

Implemented:

- `Blob` exists and is now the ingestion/readback buffer type via [`blob/include/blob/Blob.h`](../../blob/include/blob/Blob.h) and the alias in [`imager/include/imager/types/Blob.h`](../../imager/include/imager/types/Blob.h);
- `Imager::addImage` accepts `const Blob&` in [`imager/include/imager/Imager.h`](../../imager/include/imager/Imager.h);
- `Hasher` hashes a `Blob` in [`imager/src/Hasher.cpp`](../../imager/src/Hasher.cpp);
- `FileStorage::readFile` returns `Blob` in [`imager/src/FileStorage.cpp`](../../imager/src/FileStorage.cpp);
- `Imager::getImageData` returns `Blob`;
- image validation and hashing run concurrently in `Imager::addImage`;
- file writes are fanned out to all roots concurrently via `writeFileAsync`;
- file deletion is fanned out to all roots concurrently via `deleteFileAsync`;
- `listImages` and `getImagesByTags` parallelize per-image tag fetching through `Impl::enrichWithTags`.

Intentional non-implementation that matches the plan:

- public APIs remain synchronous and use `coro::blockOn` internally;
- `getImage` remains sequential;
- `getImageData` remains sequential because it depends on DB metadata first.

Gaps and caveats:

- `Blob::fromVector` is not an adoption path; it performs another copy, and the current implementation actually copies twice internally before freezing;
- `Blob::freeze()` is advisory only and does not prevent later writes through `writableData()`;
- coroutine correctness depends on the documented assumption in [`coro/include/coro/WhenAll.h`](../../coro/include/coro/WhenAll.h) that subtasks suspend before completion;
- there are no direct tests for coroutine primitives, `whenAllSettled`, `blockOn`, or parallel rollback behavior under injected failures;
- read failover from one storage root to another is implemented in `FileStorage::readFile`, but is not tested.

### `0004.REFACTORING.md`

Status: **Largely implemented**

Implemented:

- `Types.h` is now an umbrella header in [`imager/include/imager/Types.h`](../../imager/include/imager/Types.h);
- the split type headers exist:
  - [`imager/include/imager/types/ErrorCode.h`](../../imager/include/imager/types/ErrorCode.h)
  - [`imager/include/imager/types/ImageInfo.h`](../../imager/include/imager/types/ImageInfo.h)
  - [`imager/include/imager/types/AddResult.h`](../../imager/include/imager/types/AddResult.h)
  - [`imager/include/imager/types/Blob.h`](../../imager/include/imager/types/Blob.h)
- `Blob` was extracted to a top-level `blob/` library and namespace;
- coroutines were extracted to a top-level `coro/` library and namespace;
- top-level CMake includes `blob`, `coro`, and `metrics`, and `libimager` links `blob_lib`, `coro_lib`, and `metrics_lib`.

Residual drift:

- the old internal coroutine headers still remain under `imager/src/coro/`; they are stale duplicates now and appear unused by the active build, but they still create maintenance ambiguity;
- the plan described `Blob` as a generic reusable type with no project-specific dependencies, but the current `blob_lib` is linked against `metrics_lib` and the `Blob` implementation conditionally updates runtime metrics, so it is no longer purely standalone in practice.

### `0005.MONITORING.md`

Status: **Partially implemented**

Implemented:

- a dedicated `metrics/` library exists with `Histogram`, `Counter`, `Gauge`, `Timer`, snapshotting, and text formatting;
- `libimager`, `blob_lib`, and `coro_lib` all link to `metrics_lib`;
- ingestion metrics are wired into:
  - total add time,
  - validate,
  - hash,
  - dedup check,
  - mutex wait,
  - storage write total,
  - per-root storage write,
  - database insert total,
  - per-database insert,
  - storage read duration,
  - storage bytes read/written,
  - thread-pool queue depth / active threads / schedule latency,
  - images added / failed,
  - live-blob counts.
- the sample CLI can print a metrics snapshot on exit.

Missing relative to the plan:

- no `db_read_duration` or `db_write_duration` metrics are implemented in `database/src/Database.cpp`;
- no `Metrics::reset()` exists;
- no file-size correlation is recorded alongside timings, so the richer throughput analysis described in the plan is only partially possible;
- there are no tests for the metrics layer or for metric correctness under concurrent updates.

Quality concerns in the current implementation:

- `blob::Blob` updates `blob_bytes_alive` using `set(value() +/- size)`, which is not an atomic read-modify-write and can lose updates under concurrent construction/destruction;
- percentile reporting in `metrics/src/Snapshot.cpp` is bucket-based and approximate, which is acceptable, but “max” is derived from the last matching bucket boundary rather than an exact maximum;
- metrics coverage is focused on ingestion/storage; database internals are still largely uninstrumented.

### `0006.UTILITY.md`

Status: **Not implemented**

Missing:

- there is no `imagestore/` directory;
- top-level CMake does not add an `imagestore` target;
- `Imager` does not expose `validateOnly` or any dry-run/validate-and-hash API;
- there is no error-file / skip-list mechanism;
- there are no tests or sample workflows for batch import.

The existing [`imager/sample/main.cpp`](../../imager/sample/main.cpp) is a small interactive/demo CLI, not the planned batch importer.

## Codebase analysis by module

### `config/`

Current state:

- compact and aligned with the post-`0002` format;
- parser throws clear `std::runtime_error`s for missing/invalid `targets`.

Strengths:

- minimal surface area;
- parser behavior is deterministic and easy to reason about.

Gaps:

- no test target exists for config parsing;
- no semantic validation beyond presence/type checks;
- no backward compatibility shim, which matches `0002`, but the obsolete sample config still contradicts that reality.

### `database/`

Current state:

- implementation closely follows `database/CLAUDE.md`;
- schema, prepared statements, shared mutex, WAL, busy timeout, and foreign keys are all present;
- read/write API surface broadly matches the design intent;
- test coverage here is the strongest in the repo.

Strengths:

- clear RAII ownership for `sqlite3*` and `sqlite3_stmt*`;
- explicit error mapping through `DatabaseException`;
- substantial CRUD, pagination, association, and multithreading test coverage.

Gaps:

- no metrics instrumentation for read/write durations despite `0005`;
- no explicit transaction grouping at the `Database` layer beyond individual statements, which is acceptable for the current schema but limits more complex future atomic operations;
- ext values in the database tests are stored without a leading dot, while `Imager` stores dotted extensions, so there is mild convention drift between raw DB tests and facade behavior.

### `blob/`

Current state:

- generic shared-ownership buffer is implemented and already used by the facade;
- metrics hooks were added for lifetime tracking.

Strengths:

- simple API;
- cheap copy semantics once frozen;
- usable for both ingestion and readback.

Gaps:

- no direct tests;
- `fromVector` is more expensive than intended;
- freeze is a logical convention rather than an enforced invariant.

### `coro/`

Current state:

- `Task`, `ThreadPool`, `whenAll`, `whenAllSettled`, and `blockOn` exist and are actively used.

Strengths:

- small, readable, owned implementation;
- enough functionality to support the current fan-out patterns cleanly.

Gaps:

- no direct tests;
- `whenAll` explicitly relies on a fragile scheduling invariant documented in comments rather than enforced in the type/system design;
- the stale duplicate headers under `imager/src/coro/` should be removed or clearly marked obsolete.

### `metrics/`

Current state:

- useful operational instrumentation exists and is integrated into the main pipeline;
- snapshot formatting makes the data consumable without external tooling.

Strengths:

- lightweight primitives;
- instrumentation is already good enough to inspect major ingestion bottlenecks.

Gaps:

- incomplete coverage relative to the monitoring plan;
- no tests;
- one known concurrency weakness in `blob_bytes_alive`.

### `validations/`

Current state:

- JPEG and PNG validation libraries are implemented as separate wrappers around bundled libraries;
- both have focused unit tests that cover valid, wrong-format, and truncated/corrupt cases.

Strengths:

- good narrow-unit coverage for both validator wrappers;
- `imager/src/JpegValidatorImpl.cpp` and [`imager/src/PngValidatorImpl.cpp`](../../imager/src/PngValidatorImpl.cpp) provide a clean adapter layer to the facade’s `validation::IValidator`.

Gaps:

- there is no broader abstraction-level test that exercises the validators independently through `validation::IValidator`;
- CMake behavior is inconsistent across modules: database/imager tests are skipped when CppUnit is absent, while validation tests fail configuration.

### `imager/`

Current state:

- this is where the architectural plans come together successfully;
- `Imager::Impl` coordinates shared pool, multi-db, storage, validators, and metrics;
- write serialization is explicit via `writeMutex`, and read/list/search flows are simple and coherent.

Strengths:

- good composition of the lower-level modules;
- add path follows a sensible sequence: extension check, validate/hash, dedup, storage fan-out, DB fan-out;
- failure handling is broadly sound, including storage cleanup when DB insertion fails.

Important limitations:

- the add path serializes all writes under a single mutex, as expected by the monitoring plan; this protects dedup/write consistency but will cap throughput under heavy contention;
- there are no tests for multi-database rollback, partial storage failure rollback, or storage read failover;
- there are no tests for `getImageData`, `listTags`/`listImages` pagination semantics, or cross-target DB parity.

## Test coverage analysis

## Current executed test set

`ctest --test-dir build --output-on-failure` currently passes all four registered test targets:

- `DatabaseTests`
- `jpeg_validator_tests`
- `test_validate_png`
- `ImagerTests`

That means the checked-in build at `/home/vibe/src/phto/imager/build` is currently green.

## What is covered well

### Database tests

Coverage is broad and meaningful in [`database/test/DatabaseTest.cpp`](../../database/test/DatabaseTest.cpp):

- construction and open failure paths;
- file CRUD;
- tag CRUD;
- bindings and unbindings;
- pagination on files, tags, and tag lookups;
- AND-semantics tag queries;
- concurrent reads, concurrent writes, and mixed read/write workloads.

This is the most complete test suite in the repository.

### Validator tests

The validator-specific suites cover:

- null / empty / too-short inputs;
- obvious wrong-format inputs;
- truncated / corrupted payloads;
- one valid-case decode for each format.

That is appropriate unit coverage for the thin wrapper layer.

### Imager tests

The facade test suite in [`imager/test/ImagerTest.cpp`](../../imager/test/ImagerTest.cpp) covers:

- image/video ingestion basics;
- unsupported-format rejection;
- duplicate detection;
- broken-JPEG rejection;
- querying after add;
- tag creation, tagging, untagging, and searching;
- deletion;
- multi-root file copy behavior;
- concurrent `addImage` calls.

This gives good smoke/integration coverage of the happy path.

## What is not covered or only weakly covered

### No config parser tests

There are no tests for:

- valid `[[targets]]` parsing;
- malformed TOML;
- missing `targets`;
- missing `root` / `database`;
- empty `targets`;
- obsolete config examples.

Given that config format changed significantly in `0002`, this is a notable gap.

### No tests for `blob`, `coro`, or `metrics`

There is no direct coverage for:

- `Blob` freeze/copy/lifetime behavior;
- `Blob::fromVector` semantics;
- `Task`, `ThreadPool`, `whenAll`, `whenAllSettled`, or `blockOn`;
- histogram/counter/gauge/timer correctness;
- metrics snapshot formatting.

These modules are foundational and currently rely entirely on downstream behavior for validation.

### Multi-target database behavior is largely untested

The current imager suite verifies that a file lands in all storage roots, but it does not verify:

- all target databases receive the same records;
- rollback after a partial database failure;
- rollback after a partial storage failure;
- delete/tag/untag consistency across multiple target databases.

This is the largest gap relative to the goals of `0002`.

### Several facade behaviors are untested

Missing direct tests include:

- `getImageData`;
- storage-root read failover;
- pagination of `listImages`, `listTags`, and `getImagesByTags`;
- duplicate tag creation and its current API behavior;
- MOV support specifically;
- filename normalization edge cases;
- metrics side effects.

### Some imager tests are intentionally permissive

Several tests short-circuit or allow broad outcomes:

- some `addImage` tests return early if add fails instead of asserting why;
- `testAddJpeg` accepts any non-`UnsupportedFormat`/non-`StorageError` result rather than asserting full JPEG validity;
- `testAddMp4` allows `StorageError`;
- that keeps the suite robust across environments, but it also weakens its ability to detect regressions in exact behavior.

## Main mismatches and risks worth tracking

1. The docs still contain an obsolete sample config and some outdated path names.
2. The README/plan narrative says identity is hash plus size, but the active dedup key is hash only.
3. `Blob::fromVector` is more expensive than the plan implies.
4. Metrics are only partially implemented relative to `0005`, and one blob-memory metric update path is not concurrency-safe.
5. The extracted top-level `coro/` library coexists with stale unused duplicates in `imager/src/coro/`.
6. The most important architecture promised by `0002` and `0003` is only lightly validated by tests at the multi-target and rollback level.
7. `0006` is still entirely absent.

## Conclusion

The codebase is in a stronger state than the older analysis suggested. The core architecture envisioned by `0002`, `0003`, and most of `0004` is already present and coherent. The main remaining work is no longer “build the Phase 1 core”; it is:

- tighten documentation to reflect the actual API and config format;
- add tests around config parsing, coroutine primitives, metrics, and especially multi-target rollback/consistency;
- finish or explicitly defer the remaining `0005` monitoring items;
- decide whether `0006` will be implemented, and if not, update the planning docs accordingly;
- remove stale implementation remnants such as `imager/src/coro/`.
