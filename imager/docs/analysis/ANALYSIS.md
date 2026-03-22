# Imager Development Analysis

## Scope

This document compares the current implementation against the plan documents in `docs/plan/`:

- `0001.INITIAL.md`
- `0002.DATABASES.md`
- `0003.PARALLELIZING.md`

It reflects the code currently present under `config/`, `database/`, `imager/`, and `validations/`, not the older Phase 1 analysis.

## High-level status

The codebase is no longer at the `0001` baseline. It has already implemented most of the structural changes proposed in `0002` and a meaningful subset of `0003`:

- Config has moved from `[storage]` + `[database]` to `[[targets]]`.
- The facade uses `MultiDatabase` with one SQLite DB per target.
- The facade includes a custom coroutine layer (`Task`, `ThreadPool`, `whenAll`, `blockOn`).
- `Blob` has replaced raw `const uint8_t*` input for shared-ownership binary data.
- `addImage`, multi-root file writes, multi-database writes, and tag enrichment for list/search paths are parallelized.

In short: development is materially ahead of `0001`, largely aligned with `0002`, and partially aligned with `0003`.

## Match To `0001.INITIAL.md`

Status: **Implemented, but superseded in some areas**

Implemented from the initial plan:

- `libimager` facade exists with the expected core operations plus additional tag/list/count APIs.
- SQLite-backed metadata storage exists and is covered by tests.
- JPEG and PNG validation wrappers exist and are linked into the facade.
- SHA-256 hashing exists via OpenSSL.
- Redundant file storage across multiple roots exists.
- Duplicate detection exists.
- CPPUnit-based tests exist for the database, validators, and facade.
- Top-level CMake uses C++23 and builds all major subprojects.

Where the implementation has moved beyond `0001`:

- The original single database path model has been replaced by per-target databases.
- The public `addImage` API no longer takes raw pointer + size; it takes `Blob`.
- The facade now includes coroutine-based internal parallelism.

Remaining drift against `0001`:

- `0001` and `docs/plan/README.md` still describe validation libraries under `validation/`, but the repository uses `validations/`.
- `imager/imager/sample/config.toml.sample` still shows the old `[storage]` / `[database]` format, which no longer matches `config::loadConfig`.
- The initial plan emphasized streaming from disk for very large files; current hashing is chunked internally but still operates on an in-memory `Blob`, so true end-to-end streaming ingestion is not implemented.

## Match To `0002.DATABASES.md`

Status: **Largely implemented**

Implemented from `0002`:

- `config::AppConfig` now contains `std::vector<TargetConfig>` with `root` and `database`.
- `config::loadConfig` requires non-empty `[[targets]]` and validates `root` / `database` string fields.
- `MultiDatabase` exists and owns one `db::Database` per target.
- Multi-database write operations are fanned out in parallel through coroutines.
- Write paths include compensation logic for rollback on partial failure.
- Read operations are intentionally served from the first database only.
- Coroutine primitives exist in `imager/src/coro/`: `Task.h`, `ThreadPool.h`, `WhenAll.h`, `BlockOn.h`.
- `Imager` constructs and shares a single thread pool across storage and database work.

Notable details that match the plan well:

- `MultiDatabase::addFile`, `deleteFile`, `editFileName`, `addTag`, `deleteTag`, `bindTag`, and `unbindTag` all use all-or-nothing fan-out semantics.
- `FileStorage` and `MultiDatabase` both use the same coroutine/thread-pool model, which is consistent with the direction of `0002`.

Gaps or deviations from `0002`:

- The plan called for dedicated config parser tests for `[[targets]]`; there is no config test suite today.
- `loadConfig` validates presence and shape, but does not detect duplicate roots/databases or perform stronger semantic validation.
- The sample config file was not updated to the new format, which is a concrete documentation bug.
- `MultiDatabase` assumes at least one target/database and reads from `m_dbs[0]`; the config parser enforces this, but direct programmatic construction with an empty target list would still be unsafe.

## Match To `0003.PARALLELIZING.md`

Status: **Partially implemented, core pieces present**

Implemented from `0003`:

- `Blob` exists under `imager/include/imager/types/Blob.h` and is re-exported from `Types.h`.
- `Imager::addImage` now takes `const Blob&`.
- `Hasher` now hashes a `Blob`.
- `FileStorage::readFile` returns `Blob`.
- `Imager::getImageData` returns `Blob`.
- Image validation and hashing run concurrently in `Imager::addImage`.
- `FileStorage::writeFileAsync` writes to all roots in parallel and rolls back successful writes if any root fails.
- `FileStorage::deleteFileAsync` deletes from all roots in parallel.
- `listImages` and `getImagesByTags` parallelize per-file tag fetching through `Impl::enrichWithTags`.

What appears intentionally not implemented yet from `0003`:

- There is no broader public async facade; coroutine usage remains internal and is bridged back to synchronous APIs with `blockOn`.
- `getImage` remains sequential, which matches the plan's guidance.
- `getImageData` remains sequential, which also matches the plan's guidance.

Remaining gaps or risks relative to `0003`:

- `Blob::freeze()` is advisory only; there is no enforcement preventing writes after freezing.
- `Blob::fromVector` still performs a copy from the input vector, so adoption is not zero-copy.
- The coroutine helpers rely on the documented assumption that tasks suspend before completion; this matches current usage, but it is a fragile invariant for future contributors.
- There is no dedicated test coverage focused on the coroutine primitives or parallel rollback paths under injected failures.

## Current strengths

- The current architecture is coherent: shared thread pool, shared `Blob` ownership model, parallel storage writes, and parallel database fan-out fit together cleanly.
- Database functionality is well covered by tests, including multithreading.
- The existing build in `imager/build` is green: `ctest` passes all four test targets (`DatabaseTests`, `jpeg_validator_tests`, `test_validate_png`, `ImagerTests`).
- The facade implementation is ahead of the planning docs rather than behind them.

## Current issues and documentation drift

Most important current mismatches between code and docs:

1. `docs/analysis/ANALYSIS.md` was outdated and described the old single-database model.
2. `imager/imager/sample/config.toml.sample` still uses the removed `[storage]` / `[database]` format.
3. `docs/plan/README.md` and `0001.INITIAL.md` still refer to `validation/` while the repository layout is `validations/`.
4. Config parsing has no dedicated tests despite being a key compatibility boundary.

## Conclusion

The current state of development is best described as:

- `0001`: complete in substance, but no longer the active shape of the code.
- `0002`: mostly implemented.
- `0003`: partially implemented with the main architectural pieces already in place.

The main remaining work is not the original Phase 1 feature set. It is cleanup and hardening around the newer architecture: updating stale docs/examples, adding config and coroutine-focused tests, and deciding whether the project really wants true streaming ingestion rather than an in-memory `Blob` pipeline.
