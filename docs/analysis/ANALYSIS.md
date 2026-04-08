# Imager Development Analysis

## Scope

This document compares the current workspace against the specs rooted at [`CLAUDE.md`](../../CLAUDE.md) and the plan documents it points to, plus the follow-on plans that are now clearly reflected in the code:

- `docs/plan/README.md`
- `docs/plan/0001.INITIAL.md`
- `docs/plan/0002.DATABASES.md`
- `docs/plan/0003.PARALLELIZING.md`
- `docs/plan/0004.REFACTORING.md`
- `docs/plan/0005.MONITORING.md`
- `docs/plan/0006.UTILITY.md`
- `docs/plan/0007.HEIC.md`
- `docs/plan/0008.NEF.md`
- `docs/plan/0009.MOV.md`
- `docs/plan/0010.AAE.md`
- `docs/plan/0013.PROGRESS.md`
- `database/CLAUDE.md`
- `validations/jpeg/CLAUDE.md`
- `validations/png/CLAUDE.md`
- `validations/heic/CLAUDE.md`
- `validations/nef/CLAUDE.md`
- `validations/mov/CLAUDE.md`

This analysis reflects the current working tree as of 2026-04-07, including uncommitted `imagestore/` and metrics/progress changes already present in the workspace.

## Current high-level status

The codebase is materially ahead of the status summary still written in `CLAUDE.md`.

- Phase 1 core is implemented and usable.
- `0002` multi-target database fan-out is implemented.
- `0003` coroutine-based parallelism is implemented across the main ingestion/storage paths.
- `0004` refactoring into top-level `blob/`, `coro/`, and split type headers is implemented.
- `0005` monitoring is partially implemented.
- `0006` utility is now substantially implemented as `imagestore/`.
- `0007`, `0008`, `0009`, and `0010` are implemented: HEIC, NEF, MOV/MP4, and AAE support all exist in code and tests.
- `0013` progress tracking is largely implemented: new stage counters/gauges exist and `Imager::addFile()` exists.

The main remaining work is no longer "build the offline core". It is mostly:

- align top-level docs with the actual code and test set;
- add missing tests around config parsing, coroutine primitives, metrics, multi-target rollback, and `imagestore`;
- finish or explicitly defer the unimplemented parts of monitoring;
- harden some error/rollback edge cases in multi-target and sidecar flows.

## Key correspondence summary

### `CLAUDE.md`

Status: **Partially accurate, but stale**

Still accurate:

- the project is a C++23 local-network media organizer library;
- `blob/`, `coro/`, `config/`, `database/`, `imager/`, `metrics/`, and `validations/` exist as described;
- multi-root storage, per-target databases, coroutine fan-out, SHA-256 sharded storage, and sidecar handling are present;
- public APIs are synchronous while coroutines stay internal.

Stale or inaccurate:

- the implementation-status paragraph is outdated; the codebase now includes `0006` utility work, extra validators, AAE support, and progress tracking;
- the listed test suites are outdated; `ctest --preset default` currently runs 8 passing tests, not 5;
- `docs/plan/` contains more active specs than the six documents enumerated there;
- `imagestore/` exists at the repo root, but the project structure section does not mention it;
- the config sample referenced elsewhere is obsolete and still uses `[storage]` / `[database]`.

### `docs/plan/README.md`

Status: **Partially accurate**

Accurate:

- offline-first Phase 1 architecture is implemented;
- SQLite metadata and redundant storage are implemented;
- startup-only config loading is implemented;
- Phase 2 HTTP/JWT work is still absent.

Drift:

- it still says identity is SHA-256 plus size; the effective dedup key in the current facade is the SHA-256 string alone, with size stored separately;
- it still refers to `validation/` instead of `validations/`;
- it does not reflect the implemented `imagestore` utility, expanded validators, sidecar support, or progress work.

## Plan-by-plan assessment

### `0001.INITIAL.md`

Status: **Implemented in spirit, but superseded in several areas**

Implemented:

- top-level CMake builds the project as a set of focused subprojects;
- `config/`, `database/`, `imager/`, JPEG validation, PNG validation, hashing, storage, tags, lookup, listing, and deletion all exist;
- facade API exists and is coherent;
- tests exist for DB, validators, and facade behavior.

Superseded:

- config is now `[[targets]]`, not `[storage]` plus `[database]`;
- ingestion API uses `Blob`, not raw pointer plus size;
- the architecture is multi-database, not single-database;
- supported formats now include HEIC, NEF, MOV/MP4, and AAE.

Current mismatches:

- [`imager/sample/config.toml.sample`](../../imager/sample/config.toml.sample) still uses the obsolete config format;
- the document still frames MOV/MP4 as extension-only, but the current code fully validates them;
- the identity wording still suggests hash+size instead of the hash-only dedup key actually enforced.

### `0002.DATABASES.md`

Status: **Largely implemented**

Implemented:

- `config::AppConfig` now holds `std::vector<TargetConfig>`;
- config parser requires non-empty `[[targets]]`;
- `MultiDatabase` exists and opens one `db::Database` per target;
- writes are fanned out in parallel over a shared thread pool;
- rollback/compensation exists for file CRUD and tag-binding operations;
- reads intentionally go to `m_dbs[0]`.

Good alignment details:

- `Imager::Impl` owns one shared pool and passes it into storage and DB layers;
- the all-or-nothing intent is correctly expressed in `parallelWriteAll()`.

Gaps:

- there is still no direct test proving all target databases stay identical under multi-target writes;
- rollback is not fault-injected or directly tested;
- semantic config validation is still minimal: duplicate roots, duplicate database paths, and other invalid pairings are not rejected;
- `MultiDatabase` still assumes a non-empty target set on reads, relying on config validation rather than defensive construction.

### `0003.PARALLELIZING.md`

Status: **Substantially implemented**

Implemented:

- top-level `blob/` and `coro/` libraries exist and are used;
- `Imager::addImage()` accepts `Blob`;
- hashing and validation run concurrently;
- storage write/delete fan out across roots in parallel;
- tag enrichment in `listImages()` / `getImagesByTags()` is parallelized;
- synchronous facade methods bridge into coroutine internals via `blockOn()`.

Still true by design:

- `getImage()` remains sequential;
- `getImageData()` remains sequential because it depends on metadata first.

Gaps and risks:

- `Blob::fromVector()` is still more expensive than intended: it copies twice rather than adopting the vector storage;
- `Blob::freeze()` is advisory only and does not actually prevent subsequent mutable access;
- `whenAll()` still relies on the documented invariant that subtasks suspend before completion;
- there are no direct tests for `Task`, `ThreadPool`, `whenAll`, `whenAllSettled`, or `blockOn`;
- read failover in `FileStorage::readFile()` exists but is not directly tested.

### `0004.REFACTORING.md`

Status: **Implemented**

Implemented:

- `Types.h` is now an umbrella over split type headers;
- `blob/` and `coro/` are top-level libraries;
- `imager/` links against those extracted libraries;
- namespace boundaries match the directory layout.

Residual debt:

- `createDefaultValidators()` remains an inline factory in a header rather than being moved to a `.cpp`, which is minor but still noted by the repo's own coding notes.

### `0005.MONITORING.md`

Status: **Partially implemented**

Implemented:

- dedicated `metrics/` library with `Histogram`, `Counter`, `Gauge`, `Timer`, snapshots, formatting, and reset;
- instrumentation in `Imager`, `FileStorage`, and the thread-pool-facing paths;
- progress counters and in-flight gauges added per `0013`;
- metrics snapshot formatting includes pipeline progress tables and read latency.

Implemented beyond the previous analysis:

- `Metrics::reset()` now exists;
- stage counters/gauges from `0013` now exist;
- file-read latency metric `file_read` exists;
- `Imager::addFile()` is present and measures library-owned file reads.

Still missing from the original monitoring plan:

- `database/Database.cpp` still does not record `db_read_duration` or `db_write_duration`;
- `blobs_alive` and `blob_bytes_alive` are defined but not actually updated anywhere in the current `Blob` implementation;
- there are still no tests for the metrics layer;
- there is still no richer file-size correlation beyond the byte counters now added for progress reporting.

### `0006.UTILITY.md`

Status: **Substantially implemented, but not complete**

Implemented:

- `imagestore/` now exists and builds as an executable;
- stdin-driven batch import is implemented;
- `--config`, `--errors`, `--jobs`, `--dry-run`, `--verbose`, and `--help` exist;
- error-file skip-list behavior exists via `ErrorFile`;
- bounded concurrency exists via `std::counting_semaphore`;
- `Imager::validateOnly()` exists for dry-run mode;
- periodic progress reporting exists;
- final summary output exists.

Implemented beyond the original plan:

- `--quiet` and `--graph` output modes now exist in the working tree.

Still missing or divergent:

- `imagestore` still reads files itself instead of using `Imager::addFile()`, so the library-owned `read` stage metrics remain mostly unused by the utility;
- there are no tests for `imagestore`;
- the plan said duplicates are not errors and should not go to the error file, which matches the implementation;
- the default config path behavior does not explicitly error on a missing default file before parsing; it relies on config-load failure messaging instead;
- progress summary cadence differs from the exact "every 1000 files or every 5 seconds" wording in the plan.

### `0007.HEIC.md`

Status: **Implemented**

Implemented:

- `validations/heic/` exists and uses system `libheif`;
- `HeicValidatorImpl.cpp` registers `.heic` and `.heif`;
- tests exist and pass under `ctest`;
- the facade recognizes HEIC/HEIF as supported formats.

Minor caveat:

- the tests are AV1/AVIF-based fixtures run through the HEIF container path, which is acceptable for validating the container/decoder path but slightly broader than the "HEIC specifically implies HEVC" narrative in the plan.

### `0008.NEF.md`

Status: **Implemented**

Implemented:

- `validations/nef/` exists and uses system LibRaw;
- `.nef` support is wired into the validator factory;
- tests exist and pass;
- the facade accepts and validates NEF files.

### `0009.MOV.md`

Status: **Implemented**

Implemented:

- `validations/mov/` exists and uses FFmpeg libraries;
- `.mov` and `.mp4` are validated through `MovValidatorImpl.cpp`;
- tests exist and pass;
- the facade no longer treats MOV/MP4 as extension-only.

### `0010.AAE.md`

Status: **Largely implemented**

Implemented:

- `validations/aae/` exists with a dedicated validator;
- `Database` schema includes `original_name` and `file_companion`;
- `Imager::addImage()` accepts path-bearing filenames and extracts source directory plus base name;
- sidecar-parent pairing, orphan sidecars, relocation on later parent arrival, and cascade delete are implemented;
- storage for sidecars uses `storage_id` indirection;
- DB tests cover original-name and companion tables;
- imager tests cover main AAE scenarios.

Gaps and risks:

- sidecar handling is only lightly guarded on ambiguous multi-parent scenarios; it currently returns `StorageError`, which works functionally but is not a very precise error classification;
- rollback around `addOriginalName()` / `addCompanion()` is best-effort rather than fully transactional across storage and DB;
- there is no explicit multi-target test covering sidecar relocation and companion consistency across more than one database.

### `0013.PROGRESS.md`

Status: **Largely implemented**

Implemented:

- per-stage counters and gauges exist in `metrics::Metrics`;
- `Imager::addImage()` updates validation, hashing, mutex wait, dedup, storage, and DB-insert progress metrics;
- `Imager::addFile()` exists and measures read-stage progress inside the library;
- metrics snapshot formatting renders pipeline progress.

Remaining mismatch:

- `imagestore` still uses manual file reading and `addImage()` instead of switching to `addFile()`, so the read-stage metrics are not exercised by the main batch importer path yet.

## Spec correspondence by module

### `config/`

Current state:

- small and aligned with the post-`0002` target-array format;
- parser behavior is deterministic and throws readable `std::runtime_error`s.

Strengths:

- minimal surface area;
- correct structural validation for `[[targets]]`.

Gaps:

- no dedicated tests;
- no semantic validation of duplicate or contradictory target entries;
- sample config in `imager/sample/` is still wrong for the actual parser.

### `database/`

Current state:

- closely aligned with `database/CLAUDE.md`, plus additive schema extensions for sidecars;
- schema, prepared statements, shared mutex, WAL, busy timeout, and foreign keys are all present;
- this remains the strongest-tested module.

Strengths:

- good RAII around SQLite handles and statements;
- broad CRUD, pagination, multithreading, original-name, and companion coverage.

Gaps:

- no metrics instrumentation despite declared histogram fields for DB read/write timing;
- no explicit multi-database integration tests at the `MultiDatabase` level;
- some DB tests still use extension strings without a leading dot while the facade stores dotted extensions, so conventions are not perfectly uniform across layers.

### `blob/`

Current state:

- shared-ownership buffer exists and is actively used by the facade;
- API is small and workable.

Strengths:

- cheap copies after construction;
- direct writable construction path supports zero-extra-copy file reads.

Gaps:

- no direct tests;
- `fromVector()` is inefficient relative to the intended design;
- `freeze()` is not enforced;
- memory metrics promised in `0005` are not wired into `Blob`.

### `coro/`

Current state:

- `Task`, `ThreadPool`, `whenAll`, `whenAllSettled`, and `blockOn` exist and are actively used.

Strengths:

- small, owned implementation;
- sufficient for current fan-out patterns.

Gaps:

- no direct tests;
- `whenAll()` still depends on a fragile documented scheduling invariant;
- thread-pool metrics fields exist, but the current `ThreadPool` implementation does not appear to update queue-depth, active-thread, or schedule-latency metrics directly.

### `metrics/`

Current state:

- usable instrumentation and snapshot formatting exist;
- progress additions are present.

Strengths:

- compact primitives;
- good enough to observe pipeline behavior at a high level.

Gaps:

- incomplete coverage relative to the monitoring plan;
- no tests;
- defined blob and DB timing metrics are still mostly placeholders because producers are missing.

### `validations/`

Current state:

- JPEG, PNG, HEIC, NEF, MOV, and AAE validators all exist;
- each validator has its own focused test suite;
- all validator suites currently pass in the default preset.

Strengths:

- format support now matches the current plan set well;
- facade adapters are straightforward and easy to extend.

Gaps:

- there is still no abstraction-level test around `validation::IValidator` itself;
- validator test depth varies by format and environment capabilities.

### `imager/`

Current state:

- the architectural core is coherent and feature-rich;
- shared pool, multi-target DB, storage fan-out, validator registry, sidecar logic, and progress metrics are integrated in one place.

Strengths:

- add path is logically ordered and mostly robust;
- rollback on DB failure after storage write is present;
- sidecar/orphan handling is implemented instead of only planned;
- public API now includes `validateOnly()` and `addFile()`.

Important limitations:

- write serialization still happens under a single `writeMutex`, intentionally limiting throughput to preserve dedup/write consistency;
- rollback and compensation are still only partially proven by tests;
- some failure handling around sidecar bookkeeping remains best-effort rather than fully atomic.

### `imagestore/`

Current state:

- the planned batch utility exists and builds;
- it is now the main area still showing "active development" characteristics.

Strengths:

- practical CLI shape already exists;
- skip-list behavior and bounded concurrency are implemented;
- display/reporting has already evolved beyond the original plan.

Gaps:

- no test coverage;
- still duplicates some file-reading logic that the library can now own;
- current workspace contains active uncommitted UI/reporting changes, so this area is less settled than the core libraries.

## Test coverage status

## Current passing test set

`ctest --preset default --output-on-failure` currently passes all 8 registered test targets:

- `DatabaseTests`
- `jpeg_validator_tests`
- `test_validate_png`
- `heic_validator_tests`
- `nef_validator_tests`
- `mov_validator_tests`
- `aae_validator_tests`
- `ImagerTests`

This is materially broader than the older analysis and broader than the test list still mentioned in `CLAUDE.md`.

## Covered well

### Database tests

Coverage is broad:

- construction/open failure;
- file/tag CRUD;
- binding and unbinding;
- pagination;
- AND-semantics tag queries;
- multithreaded access;
- `original_name` and `file_companion` support.

### Validator tests

Each validator has focused unit coverage for valid, wrong-format, and corrupt/truncated data.

### Imager tests

The facade suite covers:

- JPEG, HEIC, NEF, MOV, and MP4 ingestion paths;
- duplicate detection;
- unsupported-format and broken-file rejection;
- querying and tags;
- deletion;
- multi-root storage;
- concurrent adds;
- AAE parent/orphan/relocation/delete scenarios.

## Still weak or missing

- no tests for `config/`;
- no direct tests for `blob/`, `coro/`, or `metrics/`;
- no tests for `imagestore/`;
- no direct tests for multi-target DB parity after successful writes;
- no direct tests for induced rollback after partial DB or storage failure;
- no direct tests for storage-root read failover;
- little or no verification of metrics correctness.

## Main mismatches and risks

1. Top-level documentation is behind the actual codebase, especially around implemented plans, tests, and `imagestore`.
2. The sample config is obsolete and does not match the actual parser.
3. The dedup identity wording in the docs still says hash+size, but the runtime uniqueness key is effectively the hash alone.
4. Monitoring is only partly realized: DB timing and blob-lifetime metrics are declared but not actually produced.
5. `Blob::fromVector()` and `Blob::freeze()` do not yet match the intended semantics/performance.
6. The most important multi-target guarantees are implemented but still under-tested in failure scenarios.
7. `imagestore` is now real and useful, but still lacks tests and has not yet been simplified to use `Imager::addFile()`.

## Conclusion

The repository is now in late Phase 1 rather than early/mid Phase 1.

The core library promised by the original specs is present, coherent, and green under the current test preset. The codebase has advanced beyond the top-level narrative: extra validators, AAE sidecars, the batch importer, and progress instrumentation are already in place. The main work left is documentation correction, stronger verification of rollback/consistency paths, and finishing the remaining monitoring and utility hardening work.
