# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++23 JPEG validation library that exposes a single function:

```cpp
enum ValidationResult {
    INVALID = -1, // the input is JPEG data that is actually broken
    VALID = 0,    // the input is valid JPEG data
    WRONG = 1,    // the input is not JPEG data at all
};

ValidationResult validateJpeg(const void* data, size_t dataSize);
```

## Build System

- **CMake 4.2.3**, C++23 standard, **clang** as the primary compiler
- libjpeg is resolved via `find_package(JPEG REQUIRED)` against the system installation
- The library must produce a `.h` header suitable for inclusion by external consumers
- Tests live in `test/` and link against **CPPUNIT** (provided externally)

## Typical Build & Test Workflow

```bash
# Configure (from repo root)
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run a single CppUnit test executable directly
./build/test/<test_binary>
```

## Architecture

- The top-level `CMakeLists.txt` calls `find_package(JPEG REQUIRED)` and `add_subdirectory(test)`.
- The validation library target links against `JPEG::JPEG` and exposes its public header.
- The `test/` target links against the validation library and the system-provided CPPUNIT.
- A test JPEG fixture (`testimg.jpg`) lives in `test/` for use by the test suite.
