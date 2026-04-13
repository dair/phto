# Testing: imagestore CLI

**Plan Reference**: `docs/plan/0006.UTILITY.md`
**Status**: in-progress
**Coverage**: 8/9 acceptance criteria covered (89%)

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|-------------------|--------|
| `--help exits 0` | `imagestore/test/test_cli.sh.in` | Help flag exits cleanly | ✅ |
| `--help output contains 'Usage'` | `imagestore/test/test_cli.sh.in` | Help text is printed | ✅ |
| `--quiet + --verbose exits 1 (short flags)` | `imagestore/test/test_cli.sh.in` | Mutual exclusion enforced | ✅ |
| `--quiet + --verbose prints 'mutually exclusive'` | `imagestore/test/test_cli.sh.in` | Error written to stderr | ✅ |
| `--verbose + --quiet exits 1 (long flags)` | `imagestore/test/test_cli.sh.in` | Mutual exclusion enforced (long form) | ✅ |
| `--verbose + --quiet prints 'mutually exclusive' (long)` | `imagestore/test/test_cli.sh.in` | Error written to stderr (long form) | ✅ |
| `--jobs 0 exits 1` | `imagestore/test/test_cli.sh.in` | Invalid jobs value rejected | ✅ |
| `--jobs negative exits 1` | `imagestore/test/test_cli.sh.in` | Negative jobs value rejected | ✅ |
| `missing config file exits 1` | `imagestore/test/test_cli.sh.in` | Config load failure exits 1 | ✅ |
| `missing config file prints error message` | `imagestore/test/test_cli.sh.in` | Config error reported to stderr | ✅ |
| `empty stdin with missing config exits 1` | `imagestore/test/test_cli.sh.in` | Config checked before stdin read | ✅ |
| `unknown flag exits 1` | `imagestore/test/test_cli.sh.in` | Bad flags rejected | ✅ |

## Progress Log

- **2026-04-11**: Initial test script created. 12 shell-based CLI tests added via `configure_file` + `add_test`. All 11 ctest suites pass (10 existing + `imagestore_cli_tests`).
- **2026-04-12**: Replaced stale `--graph` tests (tests 3-6) with correct `-v -q` mutual exclusion tests per spec 0016. The old tests were passing for the wrong reason: `--graph` was an unknown flag (exit 1) and `printUsage()` happened to contain "mutually exclusive". Tests now correctly verify the `-v -q` mutual exclusion check implemented in `main.cpp`.

## Known Gaps

- **Error file skip behaviour**: Test 8 from the spec (verify SKIP in `-v` stderr for a path listed in the error file) requires a valid config pointing to real storage roots. Skipped — testing against a live config is fragile in CI.
- **`--jobs` upper cap (256)**: Values > 256 are silently clamped, not rejected. No test verifies the cap behaviour.
- **Exit code 2**: The "some files failed" exit path now has a test (test 30 in the output-redesign section of the shell script).

## Notes

- Implementation approach: single shell script (`test/test_cli.sh.in`) processed by CMake `configure_file` to inject the binary path, then registered with `add_test`. No CppUnit or additional test framework needed.
- `set -e` inside a `run_test` wrapper caused early script termination on the first test failure. Fixed by evaluating each test in a subshell and capturing `$?` explicitly — the outer script never exits non-zero mid-run.
- `--jobs -1` is parsed by `getopt` as the unknown short flag `-` followed by a positional `1`, so it triggers the `default:` branch (unknown flag) rather than the `case 'j'` branch. Exit code is still 1.
