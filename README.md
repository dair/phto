# Imager

## Vibe

"Vibe-coded" with Claude Code (@claude); code validated and analyzed with OpenAI Codex 5.4 (@codex).

All the code verified and read through by a mere human being (i.e., @dair).
Claude paid for PRO account ($20 + extra).

All design .md-files are created by Claude Opus 4.6. All code and documentation is written by Claude Sonnet 4.6.

## Description

A C++23 library and toolset for local-network media management. Imager ingests, validates, deduplicates, and organizes photo and video collections across multiple redundant storage roots, with SQLite-based metadata and tag management.

## Features

- **Format validation** — Validates files before storage using dedicated validators for JPEG, PNG, HEIC/HEIF, Nikon NEF, MOV/MP4, and Apple AAE sidecar files
- **SHA256 deduplication** — Files are identified and deduplicated by their SHA256 content hash; duplicates are rejected without being stored
- **Multi-root redundancy** — Files are written synchronously to all configured storage roots; a failure on any root triggers a full rollback
- **Per-target SQLite databases** — Each storage root maintains its own database, kept in sync via all-or-nothing parallel writes
- **Tag-based organization** — Images can be tagged and queried by tag combinations (AND semantics) with pagination
- **AAE sidecar support** — Apple adjustment sidecars are linked to their parent by content hash and relocated automatically if they arrive before the parent image
- **Coroutine-parallel I/O** — Internal operations (validation, hashing, storage, DB writes) run concurrently via C++23 coroutines
- **Lock-free metrics** — Built-in histograms, counters, and gauges for pipeline bottleneck analysis
- **Batch import CLI** — `imagestore` reads file paths from stdin with bounded concurrency, progress reporting, dry-run mode, and an error log

## Supported Formats

| Format | Extension(s) | Validation backend |
|--------|--------------|--------------------|
| JPEG   | `.jpg`, `.jpeg` | libjpeg |
| PNG    | `.png` | libpng |
| HEIC/HEIF | `.heic`, `.heif` | libheif |
| Nikon NEF | `.nef` | LibRaw |
| MOV/MP4 | `.mov`, `.mp4` | libavformat + libavcodec |
| Apple AAE | `.aae` | Pure C++ (no external dep) |

## Requirements

- **C++23** compiler (Clang recommended; GCC supported)
- **CMake** ≥ 3.28
- System libraries: `sqlite3`, `libjpeg`, `libpng`, `libheif`, `LibRaw`, `openssl`, `libavformat`, `libavcodec`, `libavutil`
- Test framework: `cppunit` (optional; tests are skipped if absent)

On Debian/Ubuntu:

```bash
apt-get install libsqlite3-dev libjpeg-dev libpng-dev libheif-dev \
                libraw-dev libssl-dev libavformat-dev libavcodec-dev \
                libavutil-dev libcppunit-dev
```

## Building

```bash
cmake --preset default
cmake --build --preset default
```

Build artifacts are placed in `/tmp/imager-build`. To run the test suite:

```bash
ctest --preset default
```

## Configuration

Imager is configured via a TOML file. Each `[[targets]]` block pairs a storage root directory with a database file:

```toml
[[targets]]
root = "/mnt/disk1/images"
database = "/mnt/disk1/imager.db"

[[targets]]
root = "/mnt/disk2/images-backup"
database = "/mnt/disk2/imager.db"
```

Multiple targets provide redundancy — every file is written to all roots. Configuration is read once at startup.

## Library Usage

The public API lives in `imager::Imager`. Include it via `<imager/Imager.h>`.

```cpp
#include <config/Config.h>
#include <imager/Imager.h>

// Load config and construct the library
auto cfg = config::parseFile("/etc/imager.toml");
imager::Imager lib(cfg);

// Add a file from disk
imager::AddResult r = lib.addFile("/path/to/photo.jpg");
if (r.code == imager::ErrorCode::Ok) {
    std::cout << "Stored with id: " << r.id << "\n";
}

// Tag an image
lib.tagImage(r.id, "vacation");
lib.tagImage(r.id, "2024");

// Query by tags (AND semantics), paginated
auto results = lib.getImagesByTags({"vacation", "2024"}, /*offset=*/0, /*limit=*/50);

// Retrieve metadata
auto info = lib.getImage(r.id);
if (info) {
    std::cout << info->name << "  " << info->size << " bytes\n";
}

// Delete
lib.deleteImage(r.id);
```

### Key API Methods

| Method | Description |
|--------|-------------|
| `addFile(path)` | Read a file from disk and ingest it |
| `addImage(blob, filename)` | Ingest raw bytes already in memory |
| `validateOnly(blob, filename)` | Validate and hash without writing |
| `getImage(id)` | Fetch metadata and tags by SHA256 id |
| `getImagesByTags(tags, offset, limit)` | Paginated tag-based search |
| `listImages(offset, limit)` | Paginated full listing |
| `deleteImage(id)` | Remove from storage and all databases |
| `tagImage(id, tag)` / `untagImage(id, tag)` | Manage per-image tags |
| `listTags(offset, limit)` | List all known tags |
| `imageCount()` | Total number of stored files |

Return codes are defined in `imager::ErrorCode`: `Ok`, `BrokenFile`, `DuplicateFile`, `UnsupportedFormat`, `FileNotFound`, `StorageError`, `DatabaseError`, and others.

## imagestore CLI

`imagestore` is a batch import utility that reads file paths from stdin and ingests them via `libimager`.

```
Usage: imagestore [OPTIONS]

Reads file paths from stdin (one per line) and stores each via libimager.

Options:
  -c, --config PATH    Configuration file (default: ~/.imagestore.toml)
  -e, --errors PATH    Error file: skip paths listed here, append new failures
  -j, --jobs N         Concurrent processing limit (default: nproc)
  -n, --dry-run        Validate and hash only; do not write to storage or DB
  -v, --verbose        Print per-file status lines (OK/DUP/ERR/SKIP)
  -q, --quiet          Suppress all progress and summary output
      --graph          Animated pipeline graph (requires a TTY on stderr)
  -h, --help           Show usage and exit
```

Example — import a directory tree:

```bash
find /media/sdcard -type f | imagestore --config ~/imager.toml --errors failed.txt
```

Exit code `0` means all files were processed (duplicates are not errors). Exit code `2` means at least one file failed; paths are appended to the error file for later retry.

## imager_cli Demo

A minimal command-line demo (`imager_cli`) exercises the library directly:

```
imager_cli [--metrics] <config.toml> <command> [args...]

Commands:
  add    <file>
  get    <id>
  list   [--offset N] [--limit N]
  delete <id>
  tag    <id> <tag>
  untag  <id> <tag>
  tags   [--offset N] [--limit N]
  search <tag> [<tag>...]
  count
```

## Documentation

Comprehensive user documentation lives in [`docs/user/`](docs/user/):

| Guide | Description |
|-------|-------------|
| [Getting Started](docs/user/getting-started.md) | Install dependencies, build the project, run your first import |
| [Configuration Guide](docs/user/configuration.md) | TOML config format, multi-root setup, and best practices |
| [Library API Reference](docs/user/api-reference.md) | Full C++ API: all methods, types, and error codes |
| [imagestore CLI](docs/user/imagestore-cli.md) | Batch import tool: all options, usage patterns, large-collection tips |
| [imager_cli Demo](docs/user/imager-cli.md) | Interactive CLI for exploring and managing the library |
| [Supported Formats](docs/user/formats.md) | Per-format validation details, limitations, and extension list |
| [Storage and Data Model](docs/user/storage.md) | Disk layout, database schema, SHA256 sharding, AAE sidecar mechanics |
| [Metrics and Monitoring](docs/user/metrics.md) | All built-in metrics, how to read them, and bottleneck analysis |
| [Architecture Overview](docs/user/architecture.md) | Component map, concurrency model, ingestion data flow |
| [Troubleshooting](docs/user/troubleshooting.md) | Common errors, diagnosis steps, and recovery procedures |

## Project Structure

```
blob/           Shared-ownership binary buffer (namespace blob)
coro/           Coroutine primitives: Task<T>, ThreadPool, whenAll, blockOn
config/         TOML config parser
database/       SQLite wrapper (namespace db)
metrics/        Lock-free monitoring: Histogram, Counter, Gauge, Timer
validations/    Per-format validator libraries
  jpeg/  png/  heic/  nef/  mov/  aae/
imager/         libimager facade — ties everything together
  sample/       imager_cli demo
  test/
imagestore/     Batch import CLI utility
docs/
  plan/         Design documents (0001–0015)
  analysis/     Implementation status
```

## File Storage Layout

Files are stored sharded by the first two characters of their SHA256 hash:

```
<root>/
  a1/
    a1b2c3...f4.jpg
  ff/
    ffee12...ab.heic
```

AAE sidecar files use their parent image's hash as a filename prefix, preserving the pairing relationship even if the sidecar was imported before the parent.

## License

See [LICENSE](LICENSE).
