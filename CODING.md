# Coding Standards

Conventions implemented throughout this codebase. Apply them to all new and modified files.

## Formatting

**Always run `clang-format` on every file you create or modify.**

```bash
clang-format -i <file>
```

The project uses LLVM style with a 120-column line limit. A `.clang-format` at the repo root encodes this:

```yaml
BasedOnStyle: LLVM
ColumnLimit: 120
IndentWidth: 2
AccessModifierOffset: -2
```

If the file does not exist yet, create it before formatting. Never commit files that have not been passed through `clang-format`.

## Directory and file layout

Each library lives in a top-level directory. There are no `include/` or `src/` subdirectories — all headers and sources sit directly in the library directory:

```
blob/
  CMakeLists.txt
  Blob.h          ← header lives here, not in include/blob/
coro/
  CMakeLists.txt
  Task.h
  ThreadPool.h
  WhenAll.h
  BlockOn.h
metrics/
  CMakeLists.txt
  Histogram.h
  Histogram.cpp   ← source lives here, not in src/
  ...
database/
  CMakeLists.txt
  Database.h
  Database.cpp
  sample/
  test/
imager/
  CMakeLists.txt
  Imager.h
  Imager.cpp
  types/          ← sub-namespaced types get their own subdirectory
    ErrorCode.h
    ImageInfo.h
    ...
  sample/
  test/
```

`sample/` and `test/` subdirectories are the only exception — they always stay as named subdirectories regardless of nesting.

## CMake include paths

Set the include root to the **parent** of the library directory, not to a nested `include/` folder:

```cmake
# correct
target_include_directories(foo_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# wrong — never use this pattern
target_include_directories(foo_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
```

This means `foo/Bar.h` is included by consumers as `<foo/Bar.h>`, with the directory name acting as the include namespace. No double-nesting like `include/foo/Bar.h`.

## Include directives

**Ordering within a `.cpp` file** (separate each group with a blank line):

1. The `.cpp`'s own header first — `#include "MyClass.h"`
2. Project headers from other modules — `#include <module/Header.h>` (angle brackets)
3. Standard library headers — `#include <vector>` (angle brackets, alphabetical)
4. Other internal private headers — `#include "Helper.h"` (quoted)

**Angle brackets vs quotes:**
- Use `<...>` for any header found via an `-I` include path (project modules, system headers)
- Use `"..."` only for headers in the same directory or an immediate subdirectory of the current file

**Self-includes in implementation files:**
When a `.cpp` and its own `.h` are in the same directory, include the header with a bare quoted name:

```cpp
// Database.cpp
#include "Database.h"   // correct: same directory
#include <database/Database.h>  // wrong: unnecessarily qualified
```

**No most-vexing-parse:** always use brace initialization for variables whose type is a template instantiation with iterator arguments:

```cpp
// correct
std::vector<uint8_t> data{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};

// wrong — parsed as a function declaration by the compiler
std::vector<uint8_t> data(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
```

## Namespaces

Namespace names match the directory name exactly:

| Directory | Namespace |
|-----------|-----------|
| `blob/`   | `blob`    |
| `coro/`   | `coro`    |
| `config/` | `config`  |
| `database/` | `db`   |
| `metrics/` | `metrics` |
| `imager/` | `imager`  |
| `validations/jpeg/` | `validation` |
| `validations/png/`  | `validation` |

Every source file closes its namespace with a comment: `} // namespace foo`

Anonymous namespaces (`namespace { ... }`) are preferred over `static` for file-local helpers in `.cpp` files.

## Code/header separation

The header file should contain as little code as possible.

The Template code is of course in the header file.

The non-template code is allowed in the header file exclusively if it's inline function or the code of the function can fit in one line.

Otherwise the source code should be placed in the appropriate .cpp file.

## File names and rules

One header fils should contain only one class definition. Nested classes are allowed in the header file of its parent.

Enum classes should be placed in their own header files unless it's the very specific enum adjacent to the only class/function it's being used in/by.

## Naming

| Entity | Convention | Example |
|--------|-----------|---------|
| Classes, structs, enums | `PascalCase` | `FileStorage`, `ErrorCode` |
| Functions, methods | `camelCase` | `addFile`, `getTagsForFile` |
| Private member variables | `m_` prefix + `camelCase` | `m_roots`, `m_pool` |
| Local variables, parameters | `camelCase` | `fileId`, `tagName` |
| Constants, `constexpr` | `SCREAMING_SNAKE` or `PascalCase` | `SQL_CREATE_SCHEMA`, `NUM_BUCKETS` |
| Template parameters | `PascalCase` | `T`, `Op`, `Compensate` |

## Resource management

**No raw owning pointers.** All resources use RAII:

- Heap objects: `std::unique_ptr` or `std::shared_ptr`
- SQLite handles: `std::unique_ptr` with a custom deleter struct
- File handles: `std::ifstream` / `std::ofstream` (RAII by default)
- OpenSSL contexts: RAII wrapper classes

Custom deleters are defined as named structs, not lambdas, when the type alias is reused:

```cpp
struct StmtDeleter {
  void operator()(sqlite3_stmt* s) const noexcept { sqlite3_finalize(s); }
};
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;
```

**Pimpl idiom** for classes with heavy implementation: declare `struct Impl` in the header, define it in the `.cpp`. Keeps compilation dependencies and private members out of the public header.

## Error handling

Errors propagate via exceptions at library boundaries; return codes (`ErrorCode` enum) at the public facade boundary.

- Library internals (`database/`, validators) throw typed exceptions: `db::DatabaseException`
- The `imager/` facade catches those exceptions and converts them to `ErrorCode` return values
- Never throw across a `noexcept` boundary
- All `catch` blocks either handle the error specifically or rethrow; avoid bare `catch (...) {}`

Error types derive from `std::runtime_error` and carry an error-code enum for programmatic inspection:

```cpp
class DatabaseException : public std::runtime_error {
public:
  DatabaseException(DatabaseErrorCode code, const std::string& message);
  DatabaseErrorCode code() const noexcept;
private:
  DatabaseErrorCode m_code;
};
```

## Thread safety

- `std::shared_mutex` for read/write segregation: `std::shared_lock` for reads, `std::unique_lock` for writes
- `std::mutex` + `std::lock_guard` for exclusive sections (e.g., `writeMutex` in `Imager`)
- All atomic operations in `metrics/` use `std::memory_order_relaxed` for counters (commutative, order-insensitive) and `std::memory_order_acq_rel` / `std::memory_order_release` / `std::memory_order_acquire` when synchronizing state between threads
- Classes that are non-copyable and non-movable declare both pairs as `= delete` explicitly

## SQL

All SQL queries use prepared statements with bound parameters. String interpolation into SQL is never acceptable:

```cpp
// correct
sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

// forbidden
std::string sql = "SELECT * FROM file WHERE id = '" + id + "'";
```

SQL statement strings are declared as `static constexpr std::string_view` constants near the top of the implementation file, not inline at the call site.

Enable on every connection:
```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;
```

## Coroutines

Coroutine infrastructure (`coro::Task<T>`, `coro::ThreadPool`, `coro::whenAll`, `coro::blockOn`) is internal to each library that uses it. Public APIs are always synchronous — coroutines are never exposed at the public API boundary.

`coro::blockOn` bridges coroutine world to the synchronous calling thread. It is the only mechanism for crossing the boundary. Never call `blockOn` from within a coroutine (deadlock risk).

Every coroutine that runs on the thread pool must `co_await pool.schedule()` as its first suspension point. This ensures the coroutine is running on a worker thread before doing any I/O or heavy computation, and satisfies the scheduling invariant required by `whenAll`.

## Singletons

There should be NO singletons in the library.


## Validation interface

New format validators implement `validation::IValidator` and register in `imager/Validators.h`:

```cpp
// In a new XyzValidatorImpl.cpp translation unit:
class XyzValidator final : public validation::IValidator {
public:
  bool supportsExtension(const std::string& ext) const override { ... }
  validation::ValidationResult validate(const uint8_t* data, size_t size) const override { ... }
};

// In Validators.h, add:
std::unique_ptr<validation::IValidator> createXyzValidator();
// and call it in createDefaultValidators()
```

Each validator gets its own translation unit to prevent collisions between identically named C-level enums from different validation libraries.

## Bundled vs system libraries

All dependencies use system installations. No bundled library source trees exist in this project.

| Integration | When to use | Example |
|-------------|-------------|---------|
| System (`find_package`) | Preferred for all dependencies | OpenSSL, `libheif`, `libjpeg`, `libpng`, `sqlite3` |
| System (`pkg_check_modules`) | When no CMake find module exists | `libavformat`, `libavcodec`, `libavutil` |
| FetchContent | Header-only or tightly version-pinned libraries | `toml++` |

Never introduce a bundled library source tree without explicit discussion.

## Testing

- All tests use **CPPUnit** (`TestFixture` / `TestSuite` / `CPPUNIT_TEST_SUITE` pattern)
- Test databases and file storage always use `std::filesystem::temp_directory_path()` — never hardcoded paths
- Every resource created in `setUp()` or during a test is cleaned up in `tearDown()`
- Test CMakeLists gracefully skip (`return()`) when CppUnit is absent rather than failing configuration
- New format validators get a standalone test suite covering at minimum: null/empty input, wrong format, truncated/corrupt payload, and one fully valid case

## Build

```bash
cmake --preset default && cmake --build --preset default
ctest --preset default
```

Artifacts go to `/tmp/imager-build`. The compiler is Clang (`clang++`). Warnings are errors; locally the build uses `-Wall -Wextra -Wpedantic -Werror` on all first-party targets.
