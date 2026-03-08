# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++23 PNG validation library that exposes a single function:

```cpp
enum ValidationResult {
    INVALID = -1, // the input is PNG data that is actually broken
    VALID = 0,    // the input is valid PNG data
    WRONG = 1,    // the input is not PNG data at all
};

ValidationResult validatePng(const void* data, size_t dataSize);
```

## Build System

- **CMake 4.2.3**, C++23 standard, **clang** as the primary compiler
- `libpng/` — contains the bundled LibPNG dependency; 
- `libpng/src/` — **strictly read-only**, never modify these files; use libpng/src/CMakeLists.txt to build the LibPNG library
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

- The top-level `CMakeLists.txt` should `add_subdirectory(libpng)`, `add_subdirectory(sample)`, and `add_subdirectory(test)`.
- `libpng/src/CMakeLists.txt` compiles the LibPNG sources from `libpng/src/` into a static library target (e.g. `png_static`) with a well-defined include path.
- The validation library target links against that static library and exposes its public header.
- The `sample/` target is a command-line utility (`png_validate`) that links against the validation library.
- The `test/` target links against the validation library and the system-provided CPPUNIT.

## Sample utility (`sample/`)

`png_validate` is a thin command-line wrapper around `validatePng`.

```
png_validate <file>
```

It reads the named file, calls `validatePng` on its contents, and prints the result to stdout:

```
some/input/file.png: VALID
```

The result word is one of `VALID`, `INVALID`, or `WRONG`.
