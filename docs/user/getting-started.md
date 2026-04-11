# Getting Started with Imager

This guide walks you through installing system dependencies, building the project from source, writing a minimal configuration file, and running your first import.

## Prerequisites

### Compiler and Build Tools

Imager is written in C++23. You need:

- **Clang** (recommended) or **GCC** — both must support C++23 (Clang 17+ or GCC 13+)
- **CMake** 3.28 or newer
- **Ninja** or **Make** (CMake will use whichever is available)

On Debian/Ubuntu:

```bash
apt-get install clang cmake ninja-build
```

On Fedora/RHEL:

```bash
dnf install clang cmake ninja-build
```

### System Libraries

The library depends on the following system packages. All of them must be present for the build to succeed.

| Package | Debian/Ubuntu name | Fedora/RHEL name |
|---------|--------------------|------------------|
| SQLite 3 | `libsqlite3-dev` | `sqlite-devel` |
| libjpeg | `libjpeg-dev` | `libjpeg-turbo-devel` |
| libpng | `libpng-dev` | `libpng-devel` |
| libheif | `libheif-dev` | `libheif-devel` |
| LibRaw | `libraw-dev` | `LibRaw-devel` |
| OpenSSL | `libssl-dev` | `openssl-devel` |
| FFmpeg (avformat) | `libavformat-dev` | `ffmpeg-devel` |
| FFmpeg (avcodec) | `libavcodec-dev` | *(included in ffmpeg-devel)* |
| FFmpeg (avutil) | `libavutil-dev` | *(included in ffmpeg-devel)* |
| CppUnit *(optional, for tests)* | `libcppunit-dev` | `cppunit-devel` |

**Debian/Ubuntu single-line install:**

```bash
apt-get install \
  libsqlite3-dev libjpeg-dev libpng-dev libheif-dev \
  libraw-dev libssl-dev \
  libavformat-dev libavcodec-dev libavutil-dev \
  libcppunit-dev
```

If `libcppunit-dev` is absent, the test executables are simply skipped; the library and CLI tools still build normally.

## Cloning the Repository

```bash
git clone <repository-url> imager
cd imager
```

## Building

Imager uses CMake presets. The default preset compiles with Clang and places all build artifacts in `/tmp/imager-build` so the source tree stays clean.

```bash
# Configure
cmake --preset default

# Build everything (library + CLIs + tests)
cmake --build --preset default

# Alternatively, use all CPU cores explicitly
cmake --build --preset default --parallel
```

Build artifacts:

| Artifact | Location |
|----------|----------|
| `libimager.a` | `/tmp/imager-build/imager/` |
| `imager_cli` | `/tmp/imager-build/imager/sample/` |
| `imagestore` | `/tmp/imager-build/imagestore/` |
| Test executables | `/tmp/imager-build/*/test/` |

### Switching to GCC

GCC is supported but not the primary target. To use it, override the compiler when configuring:

```bash
cmake --preset default -DCMAKE_CXX_COMPILER=g++
cmake --build --preset default
```

### Building in a Different Directory

If you do not want artifacts in `/tmp`, create a custom preset or configure manually:

```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

## Running the Test Suite

```bash
ctest --preset default
```

All tests use temporary directories and clean up after themselves. A passing run should show output similar to:

```
Test project /tmp/imager-build
    Start 1: DatabaseTests
1/8 Test #1: DatabaseTests ...........  Passed   0.23 sec
    Start 2: jpeg_validator_tests
2/8 Test #2: jpeg_validator_tests ....  Passed   0.01 sec
    ...
8/8 Test #8: ImagerTests .............  Passed   0.47 sec

100% tests passed, 0 tests failed out of 8
```

To see verbose test output on failure:

```bash
ctest --preset default --output-on-failure
```

## Creating Your First Configuration File

Imager is configured with a TOML file. The file declares one or more **targets**, each pairing a storage root directory with a SQLite database file. Create `~/.imagestore.toml`:

```toml
# Single-root minimal configuration
[[targets]]
root     = "/home/alice/photos"
database = "/home/alice/photos/imager.db"
```

The root directory and the directory containing the database file must both exist before you start Imager. The database file itself is created automatically if it does not exist.

For a redundant two-root setup (recommended for any collection you care about):

```toml
[[targets]]
root     = "/mnt/primary/photos"
database = "/mnt/primary/imager.db"

[[targets]]
root     = "/mnt/backup/photos"
database = "/mnt/backup/imager.db"
```

See the [Configuration Guide](configuration.md) for a full explanation of all options and best practices.

## Importing Your First Files

Once you have a configuration file, use `imagestore` to import a directory tree:

```bash
find /media/camera -type f | imagestore --config ~/.imagestore.toml
```

`imagestore` reads file paths from standard input, one per line, and ingests each file through the full validation and deduplication pipeline. Duplicates are silently skipped (they do not count as errors). A summary is printed to stderr when processing is complete.

For a safe dry run that validates and hashes but does not write anything:

```bash
find /media/camera -type f | imagestore --config ~/.imagestore.toml --dry-run --verbose
```

See the [imagestore CLI guide](imagestore-cli.md) for all options.

## Using the Library in Your Own Application

Include the library in your CMake project:

```cmake
# In your CMakeLists.txt
find_package(imager REQUIRED)
target_link_libraries(my_app PRIVATE imager::imager)
```

Then in your code:

```cpp
#include <config/Config.h>
#include <imager/Imager.h>

int main() {
    // Load configuration
    auto cfg = config::loadConfig("/etc/myapp/imager.toml");

    // Construct the library facade
    imager::Imager lib(cfg);

    // Add a file
    imager::AddResult result = lib.addFile("/path/to/photo.jpg");
    if (result.code == imager::ErrorCode::Ok) {
        std::cout << "Stored: " << result.id << "\n";
    } else if (result.code == imager::ErrorCode::DuplicateFile) {
        std::cout << "Already exists\n";
    } else {
        std::cerr << "Error: " << result.message << "\n";
    }

    return 0;
}
```

For a complete walkthrough of the API, see the [Library API Reference](api-reference.md).

## Next Steps

- **Configure multi-root redundancy** — [Configuration Guide](configuration.md)
- **Understand error codes and what they mean** — [Library API Reference](api-reference.md#error-codes)
- **Set up batch import from your camera roll or NAS** — [imagestore CLI](imagestore-cli.md)
- **Browse your library with imager_cli** — [imager_cli Demo](imager-cli.md)
