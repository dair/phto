# Imager — Phase 1 Implementation Guide

## Project overview

A C++23 image/video organizer library (`libimager`) designed for local networks with a small number of users managing large media libraries (4–10 TB). Phase 1 is the offline core: file ingestion with validation, SHA256-based deduplication, tag-based organization, multi-root redundant storage, and a SQLite-backed metadata database.

Phase 2 (out of scope here) adds HTTP via Crow and JWT authentication.

## Toolchain & language

- **Language**: C++23 (`CMAKE_CXX_STANDARD 23`)
- **Compiler**: Clang (primary), GCC (secondary)
- **Build system**: CMake ≥ 3.28 (compatible with CMake 4.2.3)
- **Platforms**: modern Linux and macOS

## Third-party libraries

| Library | Purpose | Integration |
|---------|---------|-------------|
| **SQLite** | Metadata database | Bundled in `database/sqlite/src/` (readonly). Built as static lib via `database/sqlite/CMakeLists.txt`. DO NOT LOOK FOR SQLITE IN THE SYSTEM. |
| **libjpeg** | JPEG validation | Bundled in `validation/jpeg/libjpeg/src/` (readonly). Built as static lib. DO NOT LOOK FOR LIBJPEG IN THE SYSTEM |
| **libpng** | PNG validation | Bundled in `validation/png/libpng/src/` (readonly). Built as static lib. DO NOT LOOK FOR LIBPNG IN THE SYSTEM |
| **OpenSSL** | SHA256 hashing for file deduplication | System dependency. Found via `find_package(OpenSSL REQUIRED)` |
| **toml++** | Configuration file parsing | Header-only C++17 library. Fetch via CMake `FetchContent` from `https://github.com/marzer/tomlplusplus` |
| **CPPUnit** | Unit testing | System/external dependency. Linked for test targets |

No other libraries should be used without explicit discussion.

## Directory structure

```
Imager/
├── CLAUDE.md                    # This file
├── README.md                    # Project overview
├── CMakeLists.txt               # Top-level: builds all subprojects
├── config/                      # Configuration handling
│   ├── CMakeLists.txt
│   ├── include/config/Config.h  # Public header
│   └── src/Config.cpp
├── database/                    # SQLite database library (has its own CLAUDE.md)
│   ├── CLAUDE.md                # Detailed DB implementation guide
│   ├── CMakeLists.txt
│   ├── include/database/Database.h
│   ├── src/Database.cpp
│   ├── sqlite/                  # Bundled SQLite (readonly src/)
│   ├── sample/                  # dbcli sample app
│   └── test/                    # CPPUnit tests
├── validation/                  # Format validation libraries
│   ├── jpeg/
│   │   ├── CMakeLists.txt
│   │   ├── include/validation/JpegValidator.h
│   │   ├── src/JpegValidator.cpp
│   │   ├── libjpeg/             # Bundled libjpeg (readonly src/)
│   │   ├── sample/
│   │   └── test/
│   └── png/
│       ├── CMakeLists.txt
│       ├── include/validation/PngValidator.h
│       ├── src/PngValidator.cpp
│       ├── libpng/              # Bundled libpng (readonly src/)
│       ├── sample/
│       └── test/
├── imager/                      # Facade library (libimager)
│   ├── CMakeLists.txt
│   ├── include/imager/
│   │   ├── Imager.h             # Main facade class
│   │   ├── ImageValidator.h     # Common validation interface
│   │   └── Types.h              # Shared types (ImageInfo, Error codes, etc.)
│   ├── src/
│   │   ├── Imager.cpp
│   │   ├── ImageValidator.cpp
│   │   ├── FileStorage.cpp      # Multi-root file I/O
│   │   └── Hasher.cpp           # SHA256 via OpenSSL
│   ├── sample/                  # Small CLI demo app
│   └── test/                    # CPPUnit tests
└── test/                        # Integration tests (optional)
```

## Configuration file (TOML)

The application reads a TOML configuration file at startup. Changes are **not** hot-reloaded — a restart is required.

Example `imager.toml`:

```toml
[storage]
# Multiple root directories for redundant storage.
# Files are written synchronously to ALL roots before returning success.
roots = [
    "/mnt/disk1/images",
    "/mnt/disk2/images-backup"
]

[database]
# Path to the SQLite database file.
path = "/var/lib/imager/imager.db"
```

### Config class

```cpp
namespace config {

struct StorageConfig {
    std::vector<std::filesystem::path> roots; // At least one required
};

struct DatabaseConfig {
    std::filesystem::path path;
};

struct AppConfig {
    StorageConfig storage;
    DatabaseConfig database;
};

/// Parse config from a TOML file. Throws on missing/invalid fields.
AppConfig loadConfig(const std::filesystem::path& configPath);

} // namespace config
```

## File identity & deduplication

Each file is identified by the **SHA256 hash of its entire contents** combined with its **size in bytes**. The composite identifier is the hex-encoded SHA256 string (64 characters). File size is stored as a separate field in the database for additional collision resistance and quick metadata access.

- Use OpenSSL's EVP API (`EVP_DigestInit_ex`, `EVP_DigestUpdate`, `EVP_DigestFinal_ex` with `EVP_sha256()`) for hashing.
- Stream the file in chunks (e.g., 64 KB) to avoid loading multi-GB files into memory.
- Before storing, check if the ID already exists in the database → return duplicate error.

## Supported formats

### Images (validated)

| Format | Extension(s) | Validation library |
|--------|-------------|-------------------|
| JPEG | `.jpg`, `.jpeg` | `validation/jpeg/` via libjpeg |
| PNG | `.png` | `validation/png/` via libpng |

More image formats are expected in the near future — design the validation interface to be extensible.

### Videos (stored without deep validation)

| Format | Extension(s) | Validation |
|--------|-------------|-----------|
| MP4 | `.mp4` | Extension-based only (no deep validation in Phase 1) |
| MOV | `.mov` | Extension-based only (no deep validation in Phase 1) |

Videos are accepted based on file extension. No content validation is performed in Phase 1.

## Validation interface

A common interface that all format validators implement, allowing easy addition of new formats:

```cpp
namespace validation {

/// Result of a validation attempt.
struct ValidationResult {
    bool valid{false};
    std::string errorMessage; // Empty if valid
};

/// Abstract base for format validators.
class IValidator {
public:
    virtual ~IValidator() = default;

    /// Check whether this validator handles the given file extension.
    /// Extension is lowercase, with leading dot (e.g., ".jpg").
    virtual bool supportsExtension(const std::string& ext) const = 0;

    /// Validate raw file data. Returns success or error description.
    virtual ValidationResult validate(const uint8_t* data, size_t size) const = 0;
};

} // namespace validation
```

- `JpegValidator` implements `IValidator`, uses libjpeg's decompression API to verify JPEG integrity.
- `PngValidator` implements `IValidator`, uses libpng's `png_sig_cmp` + read API to verify PNG integrity.
- Video formats (MP4, MOV) bypass deep validation — extension check only.

## Facade library (`libimager`)

The `Imager` class is the single entry point for all operations. It composes the database, validators, file storage, and hasher internally.

```cpp
namespace imager {

enum class ErrorCode {
    Ok,
    BrokenFile,          // Validation failed
    DuplicateFile,       // SHA256+size already exists
    UnsupportedFormat,   // Extension not recognized
    FileNotFound,        // ID does not exist in the database
    StorageError,        // Filesystem I/O failure
    DatabaseError,       // Underlying DB error
    ConfigError          // Configuration problem
};

struct ImageInfo {
    std::string id;          // SHA256 hex string
    std::string name;        // Original filename
    uint64_t    size;        // File size in bytes
    std::string ext;         // Lowercase extension with dot
    std::vector<std::string> tags;
};

struct AddResult {
    ErrorCode   code;
    std::string id;          // Populated on success
    std::string message;     // Error description on failure
};

class Imager {
public:
    /// Construct from a parsed configuration.
    explicit Imager(const config::AppConfig& cfg);
    ~Imager();

    Imager(const Imager&) = delete;
    Imager& operator=(const Imager&) = delete;

    // ---- Core operations (from README) ----

    /// Add an image/video from raw data.
    /// Validates format, computes SHA256, checks for duplicates,
    /// writes to all storage roots synchronously, inserts into DB.
    /// @param data      Raw file bytes
    /// @param size      Size of data in bytes
    /// @param filename  Original filename (used to determine extension and store name)
    /// @return AddResult with id on success or error code + message on failure
    AddResult addImage(const uint8_t* data, size_t size,
                       const std::string& filename);

    /// Get image metadata + tags by ID.
    /// Returns nullopt if not found.
    std::optional<ImageInfo> getImage(const std::string& id);

    /// Get list of image IDs matching ALL given tags (AND semantics).
    /// Supports pagination via offset and limit.
    std::vector<ImageInfo> getImagesByTags(
        const std::vector<std::string>& tags,
        uint32_t offset = 0,
        uint32_t limit = 50);

    // ---- Additional operations ----

    /// Delete an image by ID. Removes from DB and all storage roots.
    ErrorCode deleteImage(const std::string& id);

    /// Add a tag to an image.
    ErrorCode tagImage(const std::string& id, const std::string& tag);

    /// Remove a tag from an image.
    ErrorCode untagImage(const std::string& id, const std::string& tag);

    /// Get all tags for an image.
    std::vector<std::string> getImageTags(const std::string& id);

    /// Get raw image data by ID (reads from first available storage root).
    /// Returns empty vector if not found.
    std::vector<uint8_t> getImageData(const std::string& id);

    /// Create a new tag in the system.
    ErrorCode createTag(const std::string& name);

    /// Delete a tag from the system (unbinds from all images).
    ErrorCode deleteTag(const std::string& name);

    /// List all tags, optionally paginated.
    std::vector<std::string> listTags(uint32_t offset = 0, uint32_t limit = 50);

    /// List all images, optionally paginated.
    std::vector<ImageInfo> listImages(uint32_t offset = 0, uint32_t limit = 50);

    /// Get total image count.
    uint64_t imageCount();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace imager
```

### `addImage` workflow

1. Extract extension from filename, lowercase it.
2. Look up a registered `IValidator` for that extension.
   - If no validator and not a known video extension → return `UnsupportedFormat`.
   - If validator found → run `validate(data, size)`. On failure → return `BrokenFile`.
   - If known video extension (`.mp4`, `.mov`) → skip validation.
3. Compute SHA256 of the data using OpenSSL EVP → this is the file `id`.
4. Check if `id` already exists in the database → return `DuplicateFile`.
5. Write the file to **all** configured storage roots synchronously.
   - File path within each root: `<root>/<first 2 chars of id>/<id>.<ext>` (sharded by hash prefix to avoid single-directory inode limits).
   - If any root write fails → clean up successfully written copies → return `StorageError`.
6. Insert the file record into the database via `db::Database::addFile(id, name, size, ext)`.
7. Return `AddResult{Ok, id, ""}`.

### File storage layout

```
<root>/
├── a1/
│   ├── a1b2c3...f4.jpg
│   └── a1ff00...e2.png
├── b3/
│   └── b3dead...01.mp4
└── ...
```

Using the first 2 hex characters of the SHA256 as a subdirectory distributes files across 256 directories, preventing filesystem performance degradation with millions of files.

## Multi-root synchronous redundancy

When writing a file:
1. Iterate over all configured storage roots.
2. Write the file to each root (create subdirectory if needed).
3. If ALL writes succeed → success.
4. If any write fails → delete the file from roots where it was already written → return error.

When reading a file:
1. Try each root in order until the file is found.
2. If no root has the file → return not found / storage error.

## Build structure

### Top-level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.28)
project(Imager LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# External dependencies
find_package(OpenSSL REQUIRED)

# toml++ via FetchContent
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# Subprojects
add_subdirectory(database)
add_subdirectory(validation/jpeg)
add_subdirectory(validation/png)
add_subdirectory(config)
add_subdirectory(imager)
```

## Testing

- Use **CPPUnit** for all unit tests (consistent with the database library).
- Each subproject has its own `test/` directory with a CPPUnit test suite.
- Use temporary directories (`std::filesystem::temp_directory_path()`) for test databases and file storage; clean up in `tearDown()`.
- The facade library tests should cover the full `addImage` workflow end-to-end.

## Implementation priorities

1. **Database library** (`database/`) — already has detailed spec in `database/CLAUDE.md`
2. **Validation libraries** (`validation/jpeg/`, `validation/png/`) — implement `IValidator` wrappers around the bundled libs
3. **Config module** (`config/`) — TOML parsing with toml++
4. **Facade library** (`imager/`) — ties everything together
5. **Tests** — unit tests for each component + integration tests for the facade

## Key design principles

- **No hot reload**: config is read once at startup.
- **Streaming hashing**: never load an entire file into memory for hashing; use chunked reads.
- **Synchronous redundancy**: all roots must be written before success is reported.
- **Extensible validation**: new formats added by implementing `IValidator` and registering with the facade.
- **Thread safety**: the database layer handles its own thread safety (see `database/CLAUDE.md`). The facade should be safe to call from multiple threads.
- **RAII everywhere**: file handles, SQLite connections, OpenSSL contexts.
- **No SQL injection**: all queries use prepared statements with bound parameters.
