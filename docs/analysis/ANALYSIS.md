# Imager Development Analysis

## Scope

This document compares the current workspace against the spec chain rooted at [`CLAUDE.md`](../../CLAUDE.md), including the dependent plans and module-level spec files it points to:

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

This analysis reflects the current working tree as of 2026-04-11.

## Executive summary

The implementation is now substantially correspondent to the Phase 1 spec set.

- The offline core described in `CLAUDE.md` and `docs/plan/0001` is implemented and testable.
- `0002` multi-target databases is implemented, including parallel fan-out and compensation logic.
- `0003` coroutine-based internal parallelism is implemented across hashing, validation, storage, DB fan-out, and tag enrichment.
- `0004` refactoring into top-level `blob/` and `coro/` libraries is implemented.
- `0005` monitoring is implemented enough to be useful in practice, but still not complete relative to the plan's full ambition.
- `0006` `imagestore` is implemented, built, and covered by a CLI test target.
- `0007` HEIC, `0008` NEF, `0009` MOV/MP4, and `0010` AAE sidecars are implemented and covered by tests.
- `0013` runtime progress tracking is implemented, including `Imager::addFile()` and pipeline-stage metrics.

The main drift is no longer missing core functionality. The remaining issues are mostly:

- stale top-level documentation;
- a few plan/doc statements that no longer match the actual implementation;
- monitoring gaps that are narrower than before but still real;
- some best-effort sidecar rollback behavior that is not fully transactional.

## Current high-level status

Compared to the older analysis, the codebase has advanced materially:

- `config_tests`, `blob_tests`, `coro_tests`, `metrics_tests`, and `imagestore_cli_tests` now exist and pass.
- multi-target parity, storage rollback, read failover, and multi-target sidecar consistency now have direct tests in [`imager/test/MultiTargetTest.cpp`](../../imager/test/MultiTargetTest.cpp).
- config semantic validation now rejects duplicate root paths and duplicate database paths.
- `Blob` now updates blob lifetime gauges when metrics are injected.
- `ThreadPool` now updates queue depth, active-thread, and schedule-latency metrics.
- `Database` now records read and write timings into `db_read_duration` and `db_write_duration`.
- `imagestore` non-dry-run mode now uses `Imager::addFile()` instead of duplicating the full non-dry-run read path itself.

That means several items previously called out as gaps are now closed.

## Key correspondence summary

### `CLAUDE.md`

Status: **Mostly accurate, but stale in several summary sections**

Accurate:

- project structure is broadly correct;
- redundant multi-root storage exists;
- per-target databases exist;
- coroutine-based internal I/O parallelism exists;
- SHA-256 sharded storage exists;
- JPEG, PNG, HEIC, NEF, MOV/MP4, and AAE validation/support exist;
- sidecar pairing/orphan relocation/cascade-delete behavior exists;
- public API remains synchronous with coroutines kept internal.

Out of date:

- the implementation-status paragraph undersells the current tree;
- the test-suite summary is stale, because the current preset now runs 13 passing tests rather than the older smaller set;
- it still frames `0005` as only partially wired without noting that DB timings, blob gauges, thread-pool metrics, config tests, blob tests, coro tests, metrics tests, and `imagestore` CLI tests now exist.

### `docs/plan/README.md`

Status: **Partially accurate**

Accurate:

- Phase 1 offline-first direction matches the code;
- Phase 2 network/HTTP work is still absent.

Drift:

- the text still says identity is SHA-256 plus size, while the runtime duplicate key is the SHA-256 ID alone and size is stored as metadata;
- it predates the implemented validator expansion, sidecar support, progress tracking, and `imagestore` utility.

## Plan-by-plan assessment

### `0001.INITIAL.md`

Status: **Implemented in spirit, with some planned details superseded by later plans**

Implemented:

- modular CMake layout exists;
- config/database/imager/validator structure exists;
- hashing via OpenSSL exists;
- redundant storage exists;
- facade API exists;
- end-to-end facade tests exist.

Superseded by later plans:

- config now uses `[[targets]]`, not older single-storage/single-db sections;
- ingestion uses `Blob`;
- storage/database are multi-target, not single-target;
- MOV/MP4 are validated, not extension-only;
- HEIC, NEF, and AAE support now exist.

Residual doc mismatch:

- the identity wording still reads like hash-plus-size even though dedup is effectively hash-only.

### `0002.DATABASES.md`

Status: **Implemented**

Implemented:

- `config::AppConfig` stores `std::vector<TargetConfig>`;
- parser requires non-empty `[[targets]]`;
- `MultiDatabase` owns one `db::Database` per target;
- writes are fanned out in parallel through a shared `ThreadPool`;
- compensation logic exists for `addFile`, `deleteFile`, `editFileName`, `addTag`, `deleteTag`, `bindTag`, and `unbindTag`;
- reads intentionally go to `m_dbs[0]`.

Now directly tested:

- successful multi-target DB parity after writes;
- storage rollback after a root write failure.

Remaining limits:

- consistency repair after an external crash remains out of scope, matching the plan's own noted risk;
- `MultiDatabase` still assumes valid non-empty target construction rather than defending every read path itself.

### `0003.PARALLELIZING.md`

Status: **Implemented**

Implemented:

- top-level `blob/` and `coro/` libraries exist;
- `addImage()` uses `Blob`;
- hashing and validation run concurrently;
- storage writes/deletes/relocations fan out in parallel;
- query-time tag enrichment is parallelized;
- synchronous facade methods bridge to internal coroutines via `blockOn()`;
- `whenAllSettled()` exists and is used for rollback-oriented flows.

Also now covered by tests:

- `ThreadPool`, `blockOn`, `whenAll`, and `whenAllSettled` have direct unit tests;
- storage read failover has direct integration coverage.

Remaining caveat:

- `whenAll()` still depends on the documented invariant that subtasks suspend before completion. The test suite exercises the intended pattern, but the implementation still documents this as an assumption rather than eliminating it structurally.

### `0004.REFACTORING.md`

Status: **Implemented**

Implemented:

- type splitting is in place;
- `Types.h` is an umbrella;
- `blob/` and `coro/` are extracted top-level libraries;
- namespace boundaries align with the directory structure.

Minor residual drift:

- `createDefaultValidators()` still lives header-side in [`imager/Validators.h`](../../imager/Validators.h), which is a small structural debt rather than a functional issue.

### `0005.MONITORING.md`

Status: **Mostly implemented, but not complete to the full plan**

Implemented:

- dedicated `metrics/` library exists with `Counter`, `Gauge`, `Histogram`, `Timer`, snapshots, formatting, and reset;
- `Imager`, `FileStorage`, `MultiDatabase`, `ThreadPool`, `Blob`, and `Database` all produce metrics now;
- stage counters and in-flight gauges from `0013` exist;
- `file_read` metrics exist through `Imager::addFile()`;
- metrics tests exist and pass.

Implemented specifically beyond the previous analysis:

- `Database` now records `db_read_duration` and `db_write_duration`;
- `Blob` now updates `blobs_alive` and `blob_bytes_alive` when metrics are supplied;
- `ThreadPool` now updates queue depth, active threads, and schedule latency.

Still incomplete relative to the plan:

- there is no compile-time metrics-disable mode described in the plan;
- there is no Prometheus/OpenTelemetry/export layer;
- instrumentation is concentrated in the core library and CLI, not in any future daemon/reporting surface.

### `0006.UTILITY.md`

Status: **Implemented**

Implemented:

- `imagestore/` exists and builds;
- stdin-driven import exists;
- `--config`, `--errors`, `--jobs`, `--dry-run`, `--verbose`, `--quiet`, `--graph`, and `--help` exist;
- bounded concurrency exists via `std::counting_semaphore`;
- error-file skip list exists;
- periodic progress reporting exists;
- final summary exists;
- CLI tests exist and pass.

Alignment improvements since the previous analysis:

- non-dry-run mode now delegates file reading to `Imager::addFile()`, so the library-owned read stage is used by the main importer path;
- `imagestore_cli_tests` is now registered in CTest and passing.

Remaining gap:

- dry-run mode still manually reads files because there is no `validateOnlyFile()` API. That is reasonable, but it means dry-run does not exercise the same library-owned read-stage metrics path as normal ingestion.

### `0007.HEIC.md`

Status: **Implemented**

Implemented:

- `validations/heic/` exists and links to system `libheif`;
- `.heic` and `.heif` support is wired into the validator registry;
- tests exist and pass.

Note:

- the fixture and validation path are acceptable for HEIF-family validation, even if the plan prose focused on HEIC/HEVC specifically.

### `0008.NEF.md`

Status: **Implemented**

Implemented:

- `validations/nef/` exists and links to LibRaw via pkg-config;
- `.nef` support is registered and tested.

### `0009.MOV.md`

Status: **Implemented**

Implemented:

- `validations/mov/` exists and links to FFmpeg libraries;
- `.mov` and `.mp4` are validated, not merely accepted by extension;
- tests exist and pass.

### `0010.AAE.md`

Status: **Largely implemented**

Implemented:

- dedicated AAE validator exists;
- DB schema includes `original_name` and `file_companion`;
- path-bearing filenames are supported;
- parent lookup, orphan sidecars, relocation after parent arrival, and cascade delete are implemented;
- sidecars are stored under `storage_id`, which may be the parent hash;
- multi-target sidecar parity is now directly tested.

Still weaker than a fully transactional design:

- sidecar-related bookkeeping across storage plus multiple DB writes is still orchestrated through best-effort compensation rather than one global transaction spanning all resources;
- parent-arrival orphan resolution intentionally does not fail the parent add if relocation/update best-effort work fails.

### `0013.PROGRESS.md`

Status: **Implemented**

Implemented:

- per-stage counters and in-flight gauges exist;
- `Imager::addImage()` updates validation/hash/mutex/dedup/storage/db stages;
- `Imager::addFile()` exists and measures the read stage;
- metrics snapshots and `imagestore --graph` consume the pipeline metrics.

Only partial remaining mismatch:

- dry-run still owns its own read path, so read-stage metrics are not unified across both normal and dry-run execution modes.

## Module-by-module assessment

### `config/`

Status: **Correspondent**

Implemented:

- parser matches `[[targets]]` config format;
- parser rejects empty or malformed target sets;
- parser now rejects duplicate roots and duplicate database paths;
- direct config tests exist and pass.

Residual note:

- validation is structural plus a small amount of semantic checking; it does not attempt to validate filesystem reachability or cross-field policy beyond duplicates.

### `database/`

Status: **Correspondent**

Implemented:

- schema and CRUD behavior align with `database/CLAUDE.md`;
- WAL, busy timeout, foreign keys, prepared statements, and shared-mutex threading are present;
- sidecar schema extensions are present;
- DB read/write metrics are now instrumented;
- test coverage is broad.

Minor mismatch:

- the original `database/CLAUDE.md` does not mention the later-added sidecar tables because they come from `0010`, so the database implementation is ahead of that narrower module-local spec.

### `blob/`

Status: **Correspondent**

Implemented:

- shared-ownership binary buffer exists;
- `fromVector()` adopts vector storage without an extra memcpy;
- freeze semantics are enforced at least in debug builds through the `assert` on `writableData()`;
- blob lifetime metrics exist when a `Metrics` pointer is supplied;
- direct blob tests exist and pass.

Residual caveat:

- immutability after `freeze()` is still a runtime convention rather than a type-level guarantee, which is consistent with the plan's wording.

### `coro/`

Status: **Correspondent**

Implemented:

- `Task`, `ThreadPool`, `whenAll`, `whenAllSettled`, and `blockOn` exist;
- thread-pool metrics are wired;
- direct coroutine tests exist and pass.

Residual caveat:

- `whenAll()` still relies on the documented "must suspend at least once" invariant.

### `metrics/`

Status: **Correspondent, but still narrower than the full monitoring vision**

Implemented:

- metric primitives, registry, reset, snapshots, and formatting exist;
- direct tests exist and pass;
- pipeline, storage, DB, pool, and blob metrics are now all represented.

Still absent relative to the broader plan:

- export/integration surfaces;
- compile-time disable path.

### `validations/`

Status: **Correspondent**

Implemented:

- JPEG, PNG, HEIC, NEF, MOV, and AAE validators exist;
- each has focused tests;
- all current validator suites pass.

### `imager/`

Status: **Strong correspondence to the current Phase 1 spec set**

Implemented:

- coherent facade over config, validators, hashing, storage, multi-DB writes, metrics, and sidecars;
- rollback from DB insert failure back into storage cleanup exists;
- multi-target parity, storage rollback, storage failover, and sidecar parity now have direct test coverage;
- `addFile()` and `validateOnly()` exist.

Known design choice:

- top-level writes are still serialized under `writeMutex` to preserve dedup/write consistency. That intentionally limits peak write throughput in exchange for correctness.

### `imagestore/`

Status: **Correspondent**

Implemented:

- batch importer shape matches the utility plan closely;
- progress and summary output are implemented;
- graph mode is implemented beyond the earlier basic plan;
- CLI tests exist and pass.

Remaining nuance:

- dry-run still performs its own file read rather than going through a library file-reading entry point.

## Test coverage status

## Current passing test set

`ctest --preset default --output-on-failure` passes all 13 registered tests as of 2026-04-11:

- `metrics_tests`
- `blob_tests`
- `coro_tests`
- `DatabaseTests`
- `jpeg_validator_tests`
- `test_validate_png`
- `heic_validator_tests`
- `nef_validator_tests`
- `mov_validator_tests`
- `aae_validator_tests`
- `config_tests`
- `ImagerTests`
- `imagestore_cli_tests`

## Areas now covered well

### Core libraries

- database CRUD, pagination, binding, multithreading, original-name, and companion behavior;
- blob ownership and metric tracking;
- coroutine primitives and scheduling behavior;
- metric primitives and reset behavior.

### Imager integration

- JPEG, HEIC, NEF, MOV, MP4, and AAE ingestion;
- duplicate detection;
- deletion;
- tags and listing;
- multi-root storage;
- concurrent adds;
- multi-target DB parity;
- rollback on a storage root failure;
- storage read failover;
- sidecar orphan resolution and multi-target companion consistency.

### CLI

- `imagestore` option handling and basic error-path behavior.

## Remaining weak spots

- there is still no fault-injection around DB-write compensation paths inside `MultiDatabase`;
- `imagestore` tests are shell-level argument/exit-path tests, not full end-to-end import workflow tests with fixture ingestion and assertions on outputs/DB state;
- the `whenAll()` invariant is tested indirectly but not structurally eliminated.

## Main mismatches and risks

1. `CLAUDE.md` and some plan summaries are stale relative to the current implementation and test surface.
2. The docs still carry the old "hash plus size identity" framing, while the runtime dedup key is the hash ID.
3. Monitoring is now broadly wired, but the full plan's export/disable features are still absent.
4. Sidecar relocation and companion updates after parent arrival remain best-effort rather than globally atomic across storage plus all databases.
5. `whenAll()` still depends on a documented coroutine scheduling invariant rather than enforcing safety mechanically.
6. `imagestore` normal mode uses `addFile()`, but dry-run still duplicates the file-read path because there is no `validateOnlyFile()` API.

## Conclusion

The current codebase is substantially correspondent to the Phase 1 specs rooted at `CLAUDE.md`.

The major implementation work described by `0001`, `0002`, `0003`, `0004`, `0006`, `0007`, `0008`, `0009`, `0010`, and `0013` is present in code and backed by passing tests. `0005` monitoring is no longer merely skeletal: it is materially implemented and integrated, though not finished to every optional endpoint/export ambition in the plan.

At this stage, the highest-value work is not building missing core behavior. It is:

- bringing top-level docs in line with the actual current implementation;
- deciding whether to keep the current best-effort sidecar resolution model or harden it further;
- deciding whether dry-run should get a library-owned file-read path;
- deciding whether the remaining `whenAll()` invariant should be eliminated in code rather than documented as a rule.
