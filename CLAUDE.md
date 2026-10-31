# Imager — Image/Video Organizer

C++23 library (`libimager`) for local-network media management. Handles file ingestion with validation, SHA256-based deduplication, tag-based organization, multi-root redundant storage, and per-target SQLite databases. All internal I/O is parallelized via C++23 coroutines.

## Build

```bash
cmake --preset default && cmake --build --preset default
ctest --preset default
```

Build artifacts go to `build/` in the repo root (configured in `CMakePresets.json`).

- **Compiler**: clang++ preferred (auto-selected), g++ fallback — both supported; override with `-DCMAKE_CXX_COMPILER=...` or `CXX` env var
- **Warnings**: `-Wall -Wextra -Wpedantic -Werror` on all first-party targets (set centrally in the root `CMakeLists.txt`; FetchContent deps are `SYSTEM`). Disable `-Werror` with `-DIMAGER_WERROR=OFF` (default `ON`), e.g. when packaging against a newer compiler
- **Standard**: C++23
- **CMake**: ≥ 3.28

## Project structure

```
├── blob/          # Header-only shared-ownership binary buffer (namespace blob)
├── coro/          # Header-only coroutine primitives: Task<T>, ThreadPool, whenAll, blockOn (namespace coro)
├── config/        # TOML config parser (toml++ via FetchContent)
├── database/      # SQLite wrapper (system SQLite via find_package — intentional system dep); one type per header: Database.h, File.h, Pagination.h, DatabaseException.h
├── validations/   # Format validators (system libjpeg/libpng/libheif/libav*; each has CLAUDE.md)
│   ├── jpeg/
│   ├── png/
│   ├── heic/
│   ├── nef/
│   ├── mov/
│   └── aae/     # Apple Adjustment Expression sidecar files
├── imager/        # Facade library (libimager) — ties everything together
│   ├── MultiDatabase.cpp   # All-or-nothing parallel writes across per-target DBs
│   ├── FileStorage.cpp     # Multi-root parallel file I/O with rollback
│   ├── Imager.cpp          # Facade: parallel validate+hash, async storage, async DB
│   ├── Hasher.cpp          # SHA256 via OpenSSL
│   ├── sample/             # CLI demo
│   └── test/
├── imagestore/    # Batch import CLI: stdin pipe, bounded concurrency, progress, error file
├── auth/          # User store (SQLite), PBKDF2 password hashing, HS256 JWT TokenService, input Validation helpers (namespace auth)
├── server/        # imagerd: Crow HTTP(S) REST daemon — routes, auth middleware, login throttle, trusted-proxy client IP, access log, streaming upload spool, native TLS (namespace server)
├── imageradmin/   # User administration CLI (add/del/passwd/enable/disable/promote/demote/list)
├── metrics/       # Lock-free monitoring: Histogram, Counter, Gauge, Timer (namespace metrics)
└── docs/
    ├── plan/      # Design documents (0001–0023)
    └── analysis/  # Implementation analysis
```

## Dependencies

| Library | Purpose | Integration |
|---------|---------|-------------|
| SQLite | Metadata DB | System — `find_package(SQLite3 REQUIRED)` (**intentional**: no bundled copy) |
| libjpeg | JPEG validation | System — `find_package(JPEG REQUIRED)` |
| libpng | PNG validation | System — `find_package(PNG REQUIRED)` |
| OpenSSL | SHA256 hashing | System — `find_package(OpenSSL REQUIRED)` |
| libheif | HEIC/HEIF validation | System — `find_package(libheif REQUIRED)` |
| *(none)* | AAE validation | Pure C++ — no external dependency |
| libavformat | MOV/MP4 container demuxing | System — `pkg_check_modules(LibAvFormat REQUIRED libavformat)` |
| libavcodec | Video codec trial decode | System — `pkg_check_modules(LibAvCodec REQUIRED libavcodec)` |
| libavutil | FFmpeg memory/error utilities | System — `pkg_check_modules(LibAvUtil REQUIRED libavutil)` |
| toml++ | Config parsing | FetchContent from GitHub |
| Crow (v1.2.1.2) | HTTP(S) server (`server/`) | FetchContent from GitHub (no Debian package); built without Boost, `CROW_ENABLE_SSL`. **Patched** at configure time with `third_party/crow/crow-body-sink.patch` (via `apply_patch.cmake`, idempotent): adds `App::header_filter` and `request::body_sink` so oversized/streamed upload bodies are never buffered. Upload policy lives in `server/Upload.cpp` |
| asio | Crow's networking layer | System — `libasio-dev`, found by Crow's `Findasio.cmake` |
| cpp-jwt | JWT issue/verify (`auth/`) | System — `find_package(cpp-jwt CONFIG REQUIRED)` (`libcpp-jwt-dev`) |
| CPPUnit | Testing | System dependency |

No other libraries without explicit discussion.

## Configuration

TOML format with `[[targets]]` — each target pairs a storage root with a database:

```toml
[[targets]]
root = "/mnt/disk1/images"
database = "/mnt/disk1/imager.db"

[[targets]]
root = "/mnt/disk2/images-backup"
database = "/mnt/disk2/imager.db"
```

Config is read once at startup (no hot reload).

## Architecture highlights

- **Multi-root redundancy**: files written synchronously to ALL roots; failure triggers rollback
- **Per-target databases**: one SQLite DB per target, kept identical via all-or-nothing parallel writes with compensation on failure
- **Coroutine parallelism**: single shared `ThreadPool` used by `MultiDatabase`, `FileStorage`, and `Imager` for concurrent I/O
- **Blob ownership**: `blob::Blob` provides shared-ownership binary buffers safe to pass into coroutines
- **File identity**: SHA256 hex string (64 chars), stored sharded by first 2 hex chars (`<root>/a1/a1b2c3...f4.jpg`)
- **Validation**: JPEG/PNG validated via system libjpeg/libpng; HEIC via system libheif; MOV/MP4 via system libavformat+libavcodec (container parse + trial decode); NEF via system LibRaw; AAE via lightweight XML/plist structure scan (no external library)
- **Sidecar files**: AAE files are stored using their parent's content hash as the filename prefix (not their own hash), preserving the pairing relationship. Orphan AAEs (added before parent) are relocated when the parent arrives. Sidecars are cascade-deleted when their parent is deleted.
- **Metrics**: always-on lock-free instrumentation (histograms, counters, gauges) for pipeline bottleneck analysis

## Testing

CPPUnit-based. Test suites (`ctest --preset default -N`, 37 tests): `metrics_tests`, `blob_tests`, `coro_tests`, `DatabaseTests`, `jpeg_validator_tests`, `test_validate_png`, `heic_validator_tests`, `nef_validator_tests`, `mov_validator_tests`, `aae_validator_tests`, `config_tests`, `auth_tests`, `auth_userstore_tests`, `auth_token_tests`, `auth_validation_tests`, `ImagerTests`, `TimestampTests`, `imagestore_cli_tests`, `imagestore_memcheck`, `imagestore_output_tests`, `imageradmin_cli_tests`, and the server tests `server_health_test`, `server_auth_test`, `server_authz_test`, `server_json_tests`, `server_clientip_tests` (CPPUnit: client IP / `X-Forwarded-For`, access-log format), `server_items_test`, `server_content_test`, `server_list_test`, `server_item_tags_test`, `server_tags_test`, `server_users_test`, `server_hardening_test`, `server_tls_test`, `server_api_test`, `server_fixups_test`, `server_loose_ends_test` (shell tests run a live `imagerd`).

Use temporary directories for test databases and file storage; clean up in `tearDown()`.

## Design documents

Detailed plans live in `docs/plan/`:

- **[README](docs/plan/README.md)** — High-level project goals (Phase 1: offline core, Phase 2: HTTP+JWT)
- **[0001.INITIAL](docs/plan/0001.INITIAL.md)** — Phase 1 implementation guide: directory structure, config, file identity, validation interface, facade API, build structure
- **[0002.DATABASES](docs/plan/0002.DATABASES.md)** — Per-target SQLite databases with coroutine-based parallel writes and compensation rollback
- **[0003.PARALLELIZING](docs/plan/0003.PARALLELIZING.md)** — Whole-library coroutine parallelization: Blob type, async FileStorage, parallel validate+hash, parallel tag fan-out
- **[0004.REFACTORING](docs/plan/0004.REFACTORING.md)** — Type splitting (Types.h → individual headers) and library extraction (coro/, blob/ as top-level libs)
- **[0005.MONITORING](docs/plan/0005.MONITORING.md)** — Lock-free metrics infrastructure for pipeline bottleneck analysis
- **[0006.UTILITY](docs/plan/0006.UTILITY.md)** — `imagestore` batch import CLI utility (stdin pipe, bounded concurrency, error file, dry-run)
- **[0007.HEIC](docs/plan/0007.HEIC.md)** — HEIC/HEIF format validation via libheif
- **[0008.NEF](docs/plan/0008.NEF.md)** — Nikon NEF raw format validation via LibRaw
- **[0009.MOV](docs/plan/0009.MOV.md)** — MOV/MP4 container validation via libavformat + libavcodec
- **[0010.AAE](docs/plan/0010.AAE.md)** — Apple AAE sidecar support: parent-hash storage, orphan relocation, cascade delete
- **[0011.ARCHIVES](docs/plan/0011.ARCHIVES.md)** — Archive format support (planned)
- **[0012.CODING_STANDARDS](docs/plan/0012.CODING_STANDARDS.md)** — Project coding standards reference
- **[0013.PROGRESS](docs/plan/0013.PROGRESS.md)** — Progress tracking: stage counters, `--graph` mode, `Imager::addFile()`
- **[0014.DISPLAY](docs/plan/0014.DISPLAY.md)** — Display and output improvements for imagestore
- **[0015.GAPS](docs/plan/0015.GAPS.md)** — Gap analysis and remediation plan
- **[0016.VERBOSE_OUTPUT](docs/plan/0016.VERBOSE_OUTPUT.md)** — Verbose output redesign: real-time file status display
- **[0017.LARGE_FILES](docs/plan/0017.LARGE_FILES.md)** — Large file handling: true streaming processing
- **[0018.MEMORY](docs/plan/0018.MEMORY.md)** — Memory growth investigation and instrumentation
- **[0019.DELETE](docs/plan/0019.DELETE.md)** — File-input deletion in `libimager` and `imagestore`
- **[0020.TIMESTAMPS](docs/plan/0020.TIMESTAMPS.md)** — Preserve original file timestamps
- **[0021.REORGANIZATION](docs/plan/0021.REORGANIZATION.md)** — Workspace/submodule reorganization
- **[0022.SERVER](docs/plan/0022.SERVER.md)** — Phase 2 HTTP(S) REST server: Crow, JWT auth, users, items, tags, TLS
- **[0023.TRANSCODING](docs/plan/0023.TRANSCODING.md)** — Content transcoding and format negotiation for `GET /items/{id}/content` (proposal, not implemented)

Implementation status is tracked in **[docs/analysis/ANALYSIS.md](docs/analysis/ANALYSIS.md)** — Phase 1 core complete; 0002 (multi-target DB), 0003 (coroutine parallelism), 0004 (refactoring), 0007–0010 (HEIC/NEF/MOV/AAE validators), and 0013 (progress tracking) implemented. 0005 (metrics) partially wired. 0006 (`imagestore` CLI) substantially implemented. **Phase 2 (0022) is implemented** (`auth/`, `server/`/`imagerd`, `imageradmin`; checkpoints A1–J4); see [docs/user/API.md](docs/user/API.md) and [docs/user/DEPLOYMENT.md](docs/user/DEPLOYMENT.md). ANALYSIS.md covers Phase 1 in detail and has a dated Phase 2 summary section.

## Key conventions

- Namespaces match directory: `auth::`, `blob::`, `coro::`, `config::`, `db::`, `imager::`, `metrics::`, `server::`, `validation::`
- All SQL uses prepared statements with bound parameters (no string interpolation)
- RAII for all resources (file handles, SQLite connections, OpenSSL contexts)
- Public API is synchronous; coroutines are internal, bridged via `coro::blockOn`
