# Testing Conventions

How tests are structured, built, and run in this project. For per-feature
coverage status, see the other docs in this directory and
[`README.md`](README.md). For non-obvious implementation knowledge see
[`../DEVELOPMENT_NOTES.md`](../DEVELOPMENT_NOTES.md).

## Framework & layout

- **CPPUnit** for C++ unit tests; each module keeps its tests in `<module>/test/`.
- **Shell scripts** for CLI binaries (argument handling, exit codes, output).

## Build & run

```bash
cmake --preset default                 # configures into ./build (in-source; see CMakePresets.json)
cmake --build --preset default
ctest --preset default                 # full suite
ctest --preset default -R <name> -V    # one test, verbose
```

After adding a new `test/` subdirectory you must re-run `cmake --preset default`
before the new target is visible. (Build artifacts live under `build/` in the
source tree — notes referencing `/tmp/imager-build` are stale.)

## Test `CMakeLists.txt` pattern

Mirror `database/test/CMakeLists.txt`: discover CppUnit via pkg-config with a
`find_library(CPPUNIT_LIB cppunit)` fallback and **skip gracefully** when it is
absent; `add_executable`; `target_include_directories` for the CppUnit headers;
`target_link_libraries` against the module library + CppUnit; register with
`add_test`. The parent `CMakeLists.txt` adds `add_subdirectory(test)`.

## Conventions & gotchas

- **One `main()` per test binary.** When several `.cpp` files compile into one
  test executable, exactly one owns `main()` (e.g. `imager/test/ImagerTest.cpp`);
  the others omit it and rely on `CPPUNIT_TEST_SUITE_REGISTRATION`, which the
  registry discovers automatically. Some newer modules (e.g. `auth/test/`) instead
  use one executable per test file — both patterns are acceptable.
- **Helpers are copied, not shared.** Test helpers (e.g. `uniqueSuffix`,
  `makeMinimalJpeg`, fixture loaders) are duplicated per test `.cpp` rather than
  shared via a header — an explicit project convention.
- **File-based tests:** write temp inputs with `std::ofstream` in `setUp()`,
  remove them in `tearDown()`, using
  `std::filesystem::temp_directory_path() / "unique_name"`.
- **Permission-based fault injection:** tests that use
  `fs::permissions(path, fs::perms::none)` to simulate I/O failures must skip when
  `geteuid() == 0` (root bypasses permission checks) and must restore permissions
  in `tearDown()` *before* `fs::remove_all`, or cleanup fails.
- **Multi-target DB inspection:** to verify per-target database state, open each
  DB file directly with `db::Database(path)` — this bypasses `MultiDatabase`'s
  read-from-first-only policy.
- **Shell CLI tests:** templated as `test/*.sh.in`, materialized via
  `configure_file` with the binary path injected (`$<TARGET_FILE:…>` or an
  `@…_BIN@` substitution) and registered with `add_test`. **Wrap each individual
  check in a subshell** so one failure under `set -e` doesn't abort the whole
  script.

## Valgrind memory regression test

`imagestore_memcheck` (declared in `imagestore/CMakeLists.txt`, guarded by
`find_program(VALGRIND valgrind)`) runs the import pipeline under Valgrind with
`--leak-check=full --errors-for-leak-kinds=definite,indirect --error-exitcode=1`
and `--suppressions=valgrind.supp`. The suppression file covers third-party
still-reachable allocations (glib, libheif, libav*, libgomp). It exercises both
the new-file and duplicate-import paths; the baseline is 0 definite/indirect bytes
lost (runtime ~13 s, 120 s CTest timeout). It is the safety net for the coroutine
frame-lifetime rule documented in
[`../DEVELOPMENT_NOTES.md`](../DEVELOPMENT_NOTES.md).
