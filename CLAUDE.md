# Database Library — Implementation Guide

## Project overview

A C++23 SQLite-backed database library for an image processor. Single public class `Database` managing files, tags, and their associations. Must be thread-safe.

## Toolchain

- **Language**: C++23, targeting Clang
- **Build system**: CMake 4.2.3
- **Dependencies**: SQLite (bundled in `sqlite/src/`), C++ Standard Library + STL, CPPUnit (external, for tests)
- **No other libraries** unless explicitly discussed first

## Directory structure

```
database/
├── CLAUDE.md
├── README.md
├── CMakeLists.txt              # Top-level: builds library + tests
├── include/
│   └── database/
│       └── Database.h          # Public header
├── src/
│   └── Database.cpp            # Implementation
├── sqlite/
│   ├── CMakeLists.txt          # Builds SQLite as a static library
│   └── src/                    # READONLY — do not modify anything here
│       ├── src/                # C sources (main.c, sqlite.h.in, etc.)
│       └── main.mk             # Amalgamation build rules
└── test/
    ├── CMakeLists.txt          # Test executable, links CPPUnit
    └── DatabaseTest.cpp        # CPPUnit test suite
```

## SQLite build (`sqlite/CMakeLists.txt`)

The bundled SQLite is a **full source tree**, not the amalgamation. The CMakeLists.txt in `sqlite/` must:

1. Generate the amalgamation (`sqlite3.c` and `sqlite3.h`) using the source tree's `main.mk` / `mksqlite3c.tcl`, **or** compile the individual source files directly.
2. Produce a static library target (e.g., `sqlite3_lib`).
3. Export an interface include path so consumers can `#include <sqlite3.h>` (or `#include "sqlite3.h"`).
4. Compile with `-DSQLITE_THREADSAFE=1` (the default, but be explicit) to support multi-threaded usage.
5. **Never modify files under `sqlite/src/`**.

Simplest approach: use the Tcl-based amalgamation generator (`tool/mksqlite3c.tcl` if present), or as a fallback, glob all `.c` files under `sqlite/src/src/` excluding `test*`, `tclsqlite*`, and `shell.c.in`, and compile them together. Prefer the amalgamation route if feasible.

## Schema

```sql
CREATE TABLE IF NOT EXISTS file (
    id   TEXT PRIMARY KEY NOT NULL,
    name TEXT NOT NULL,
    size INTEGER NOT NULL,  -- unsigned 64-bit stored as SQLite INTEGER (64-bit signed, sufficient)
    ext  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS tag (
    name TEXT PRIMARY KEY NOT NULL
);

CREATE TABLE IF NOT EXISTS file_tag (
    file_id  TEXT NOT NULL REFERENCES file(id) ON DELETE CASCADE,
    tag_name TEXT NOT NULL REFERENCES tag(name) ON DELETE CASCADE,
    PRIMARY KEY (file_id, tag_name)
);
```

Enable foreign keys on every connection: `PRAGMA foreign_keys = ON;`

## Public API (`Database` class)

### Constructor / destructor

- `explicit Database(const std::filesystem::path& dbPath)` — opens or creates the database.
  - If file doesn't exist → create and initialize schema. On failure → throw with error code (e.g., `DatabaseError::CreationFailed`).
  - If file exists → open for read/write. On failure → throw with error code (e.g., `DatabaseError::OpenFailed`).
- Destructor closes the connection cleanly.
- Non-copyable, movable.

### Core methods (from README)

| Method | Signature sketch |
|---|---|
| Add file | `void addFile(const std::string& id, const std::string& name, uint64_t size, const std::string& ext)` |
| Delete file | `void deleteFile(const std::string& id)` |
| Edit file name | `void editFileName(const std::string& id, const std::string& newName)` |
| Add tag | `void addTag(const std::string& name)` |
| Delete tag | `void deleteTag(const std::string& name)` |
| Bind tag to file | `void bindTag(const std::string& fileId, const std::string& tagName)` |
| Unbind tag from file | `void unbindTag(const std::string& fileId, const std::string& tagName)` |
| Get tags for file | `std::vector<std::string> getTagsForFile(const std::string& fileId, std::optional<Pagination> page = std::nullopt)` |
| Get files by tags | `std::vector<File> getFilesByTags(const std::vector<std::string>& tagNames, std::optional<Pagination> page = std::nullopt)` |

### Additional recommended methods

- `std::optional<File> getFile(const std::string& id)` — fetch a single file record
- `std::vector<File> getAllFiles(std::optional<Pagination> page = std::nullopt)` — list all files
- `std::vector<std::string> getAllTags(std::optional<Pagination> page = std::nullopt)` — list all tags
- `bool fileExists(const std::string& id)` / `bool tagExists(const std::string& name)` — existence checks
- `uint64_t fileCount()` / `uint64_t tagCount()` — counts

### Supporting types

```cpp
struct File {
    std::string id;
    std::string name;
    uint64_t size;
    std::string ext;
};

struct Pagination {
    uint32_t offset = 0;
    uint32_t limit = 50;  // sensible default page size
};
```

### Pagination approach

Use SQL `LIMIT ? OFFSET ?`. The `Pagination` struct is optional on query methods — when omitted, return all results. This keeps the API simple while allowing callers to paginate large result sets.

### Error handling

Define a custom exception hierarchy:

```cpp
enum class DatabaseErrorCode {
    CreationFailed,
    OpenFailed,
    QueryFailed,
    NotFound,
    ConstraintViolation
};

class DatabaseException : public std::runtime_error {
public:
    DatabaseException(DatabaseErrorCode code, const std::string& message);
    DatabaseErrorCode code() const noexcept;
};
```

## Thread safety

- SQLite is compiled with `SQLITE_THREADSAFE=1` (serialized mode).
- Use **WAL journal mode** (`PRAGMA journal_mode=WAL;`) for concurrent readers + single writer.
- Protect all database access with a `std::shared_mutex`:
  - Read operations take `std::shared_lock` (multiple concurrent readers).
  - Write operations take `std::unique_lock` (exclusive access).
- Use `PRAGMA busy_timeout = 5000;` to handle lock contention at the SQLite level.
- All SQLite statements should use prepared statements (`sqlite3_prepare_v2`) with bound parameters to prevent SQL injection and improve performance.

## CMake structure

**Top-level `CMakeLists.txt`**:
- `cmake_minimum_required(VERSION 3.28)` (CMake 4.2.3 compatible)
- `project(DatabaseLib LANGUAGES C CXX)`
- `set(CMAKE_CXX_STANDARD 23)`, `set(CMAKE_CXX_STANDARD_REQUIRED ON)`
- `add_subdirectory(sqlite)` — builds the SQLite static library
- `add_library(database src/Database.cpp)` with public include dir `include/`
- `target_link_libraries(database PRIVATE sqlite3_lib)`
- `add_subdirectory(test)`

**`test/CMakeLists.txt`**:
- `add_executable(database_tests DatabaseTest.cpp)`
- `target_link_libraries(database_tests PRIVATE database cppunit)`
- Register with CTest

## Testing (`test/DatabaseTest.cpp`)

Use CPPUnit's `TestFixture` / `TestSuite` pattern. Cover:

1. **Construction**: create new DB, open existing DB, fail on invalid path
2. **File CRUD**: add, get, edit name, delete, verify cascade on file_tag
3. **Tag CRUD**: add, get, delete, verify cascade on file_tag
4. **Bindings**: bind/unbind, get tags for file, get files by tags, multi-tag queries
5. **Pagination**: verify offset/limit behavior, edge cases (offset beyond results)
6. **Error paths**: duplicate keys, not-found deletes, constraint violations
7. **Multithreading**: concurrent reads, concurrent writes, mixed read/write load, verify no data corruption or deadlocks under heavy contention (use `std::thread` / `std::jthread`, run many iterations)

Use temporary files (`std::filesystem::temp_directory_path()`) for test databases, clean up in `tearDown()`.

## Implementation notes

- Wrap `sqlite3*` and `sqlite3_stmt*` in RAII wrappers or use unique_ptr with custom deleters to prevent resource leaks.
- Use `sqlite3_bind_*` for all parameter binding — never interpolate strings into SQL.
- `uint64_t` maps to SQLite INTEGER (64-bit signed). Values up to 2^63-1 fit naturally. Document this range limit.
- Keep all SQL as `constexpr std::string_view` constants near the top of the implementation file.
