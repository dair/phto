# Testing: imagestore CLI

**Plan Reference**: `docs/plan/0006.UTILITY.md`
**Status**: in-progress
**Coverage**: 8/9 acceptance criteria covered (89%)

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|-------------------|--------|
| `--help exits 0` | `imagestore/test/test_cli.sh.in` | Help flag exits cleanly | ✅ |
| `--help output contains 'Usage'` | `imagestore/test/test_cli.sh.in` | Help text is printed | ✅ |
| `--quiet + --graph exits 1` | `imagestore/test/test_cli.sh.in` | Mutual exclusion enforced | ✅ |
| `--quiet + --graph prints error message` | `imagestore/test/test_cli.sh.in` | Error written to stderr | ✅ |
| `--graph + --verbose exits 1` | `imagestore/test/test_cli.sh.in` | Mutual exclusion enforced | ✅ |
| `--graph + --verbose prints error message` | `imagestore/test/test_cli.sh.in` | Error written to stderr | ✅ |
| `--jobs 0 exits 1` | `imagestore/test/test_cli.sh.in` | Invalid jobs value rejected | ✅ |
| `--jobs negative exits 1` | `imagestore/test/test_cli.sh.in` | Negative jobs value rejected | ✅ |
| `missing config file exits 1` | `imagestore/test/test_cli.sh.in` | Config load failure exits 1 | ✅ |
| `missing config file prints error message` | `imagestore/test/test_cli.sh.in` | Config error reported to stderr | ✅ |
| `empty stdin with missing config exits 1` | `imagestore/test/test_cli.sh.in` | Config checked before stdin read | ✅ |
| `unknown flag exits 1` | `imagestore/test/test_cli.sh.in` | Bad flags rejected | ✅ |

## Progress Log

- **2026-04-11**: Initial test script created. 12 shell-based CLI tests added via `configure_file` + `add_test`. All 11 ctest suites pass (10 existing + `imagestore_cli_tests`).

## Known Gaps

- **Error file skip behaviour**: Test 8 from the spec (verify SKIP in `-v` stderr for a path listed in the error file) requires a valid config pointing to real storage roots. Skipped — testing against a live config is fragile in CI.
- **Dry-run mode**: `--dry-run` with real input files not tested; requires a valid config.
- **`--jobs` upper cap (256)**: Values > 256 are silently clamped, not rejected. No test verifies the cap behaviour.
- **`--graph` TTY fallback**: The spec says `--graph` falls back to normal mode when stderr is not a TTY. Shell tests always run without a TTY, so this path is exercised but not explicitly asserted.
- **Exit code 2**: The "some files failed" exit path is only reached after storage operations, which require a real config. Not tested here.

## Notes

- Implementation approach: single shell script (`test/test_cli.sh.in`) processed by CMake `configure_file` to inject the binary path, then registered with `add_test`. No CppUnit or additional test framework needed.
- `set -e` inside a `run_test` wrapper caused early script termination on the first test failure. Fixed by evaluating each test in a subshell and capturing `$?` explicitly — the outer script never exits non-zero mid-run.
- `--jobs -1` is parsed by `getopt` as the unknown short flag `-` followed by a positional `1`, so it triggers the `default:` branch (unknown flag) rather than the `case 'j'` branch. Exit code is still 1.
