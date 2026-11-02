# imagestore CLI

`imagestore` is the primary tool for bulk importing photo and video collections into Imager. It reads file paths from standard input, one per line, and ingests each through the full `libimager` validation and deduplication pipeline with configurable parallelism, progress reporting, and error tracking.

## Synopsis

```
imagestore [OPTIONS]

Reads file paths from stdin (one per line) and stores each via libimager.
```

## Options

| Option | Short | Description |
|--------|-------|-------------|
| `--config PATH` | `-c` | Path to the TOML configuration file. Default: `~/.imagestore.toml` |
| `--errors PATH` | `-e` | Error file. Paths listed here are skipped on startup; new failures are appended. |
| `--jobs N` | `-j` | Maximum number of files processed concurrently. Default: number of CPU cores (capped at 256). |
| `--dry-run` | `-n` | Validate and hash only; do not write to storage or databases. |
| `--delete` | `-d` | Delete mode: treat stdin paths as files to remove from the collection. Each file is read and hashed, and the matching id is removed from all databases and storage. No validation is performed. |
| `--verbose` | `-v` | Real-time slot display on a TTY, otherwise one status line per file on stderr: `OK`, `DUP`, `ERR`, or `SKIP`. |
| `--quiet` | `-q` | Suppress all progress and summary output. |
| `--memory-report` | | Periodically log RSS and libimager memory gauges to stderr (`MEM` lines, final `MEMF` line). Cannot be combined with `--quiet`. Stops immediately when the import finishes. |
| `--memory-interval N` | | Seconds between memory reports. Default: 5. |
| `--help` | `-h` | Print usage and exit. |

### Mutually Exclusive Options

- `--quiet` and `--verbose` cannot be used together.
- `--quiet` and `--memory-report` cannot be used together.
- In verbose mode, a TTY on stderr gives a real-time slot display; otherwise `imagestore` prints one line per file.
- With `--delete`, `--dry-run` computes hashes and checks existence without deleting.

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | All files processed successfully. Duplicates are not errors. |
| `1` | Usage error or configuration failure. |
| `2` | One or more files failed to ingest. |

## Input Format

`imagestore` reads one file path per line from standard input. Lines are trimmed of leading and trailing whitespace. Empty lines and lines beginning with `#` are skipped.

Paths are resolved to absolute form before processing. Relative paths are interpreted relative to the working directory at the time `imagestore` is invoked.

## Basic Usage Examples

### Import a Directory Tree

```bash
find /media/camera -type f | imagestore --config ~/.imagestore.toml
```

### Import Only Recognized Media Formats

```bash
find /media/camera -type f \( -iname "*.jpg" -o -iname "*.jpeg" \
  -o -iname "*.png" -o -iname "*.heic" -o -iname "*.heif" \
  -o -iname "*.nef" -o -iname "*.mov" -o -iname "*.mp4" \
  -o -iname "*.aae" \) | imagestore --config ~/.imagestore.toml
```

Passing unsupported extensions is harmless — they are rejected with `UnsupportedFormat` — but pre-filtering reduces unnecessary I/O.

### Dry Run Before Committing

Always a good idea for a first import:

```bash
find /media/sdcard -type f | imagestore \
  --config ~/.imagestore.toml \
  --dry-run \
  --verbose
```

This validates every file and reports what would be added, duplicated, or rejected, without modifying any data.

### Verbose Mode to See Per-File Status

```bash
find /home/alice/photos -type f | imagestore \
  --config ~/.imagestore.toml \
  --verbose 2>import.log
```

Each line in `import.log` will be one of:

```
OK   a1b2c3...f4 /home/alice/photos/vacation/IMG_0001.JPG
DUP  /home/alice/photos/vacation/IMG_0001.JPG
ERR  /home/alice/photos/vacation/IMG_broken.jpg: BrokenFile: JPEG data is corrupt
SKIP /home/alice/photos/vacation/IMG_0002.JPG (in error list)
```

### Limiting Concurrency

On a system with many cores, the default job count may saturate a slow NAS. Limit to 4 concurrent jobs:

```bash
find /mnt/nas -type f | imagestore --config ~/.imagestore.toml --jobs 4
```

## The Error File (`--errors`)

The error file serves two purposes at once:

1. **Skip list** — on startup, `imagestore` reads the file and skips any paths it contains. This lets you resume an interrupted import without re-attempting files that already failed.
2. **Failure log** — new failures during the current run are appended to the file.

Workflow for a resumable import:

```bash
# First run — create the error file
find /media/camera -type f | imagestore \
  --config ~/.imagestore.toml \
  --errors ~/import-errors.txt

# Fix whatever caused the failures, then retry only the failed files
cat ~/import-errors.txt | imagestore \
  --config ~/.imagestore.toml \
  --errors ~/import-errors.txt
```

On the second run, previously-successful files that happen to appear in the error file are not re-processed (paths are deduped within the skip set). New failures are appended again.

Note that `DuplicateFile` results are never written to the error file — duplicates are expected and benign.

## Progress Output

In the default (non-quiet, non-verbose) mode, `imagestore` periodically prints a progress summary to stderr:

```
[imagestore] 1234/5678 files  |  OK 1100  DUP 120  ERR 14  |  2.3 GB  |  3.4 files/s
```

A final summary is always printed after all files are processed:

```
[imagestore] Done in 38.4s
  5678 processed  |  OK 5520  DUP 130  ERR 28  SKIP 0
  4.1 GB  |  147.9 files/s
```


## AAE Sidecar Files

Apple AAE sidecar files (`.aae`) pair with their parent image by matching base filename within the same source directory. For correct pairing, pass file paths that include at least their immediate parent directory. When using `find`, the path already includes the directory:

```bash
# Good: paths include directory, enabling pairing
find /photos/vacation -type f | imagestore --config ~/.imagestore.toml

# Works but no pairing: bare filenames
ls /photos/vacation | imagestore --config ~/.imagestore.toml
```

If an `.aae` file arrives before its parent image, it is stored as an orphan. When the parent image is imported later, the orphan sidecar is automatically relocated to use the parent's hash as its storage filename. See [Storage and Data Model: AAE Sidecars](storage.md#aae-sidecars) for details.

## Large Collections: Best Practices

### Estimate the Import Size First

Use `find` with `-ls` or `du` to get a sense of what you are importing before starting:

```bash
find /media/camera -type f | wc -l        # file count
find /media/camera -type f | xargs du -sh  # rough size
```

### Use a Dedicated Error File

Always use `--errors` for large imports. Without it, you cannot easily identify and retry failed files.

### Run Imports in a Screen/tmux Session

Imports of large collections can take hours. Run in a persistent terminal session:

```bash
tmux new-session -s import
find /media/camera -type f | imagestore --config ~/.imagestore.toml --errors ~/errors.txt
```

### Monitor Progress with Verbose Mode

For long imports on a terminal, `--verbose` gives a real-time per-slot display:

```bash
find /media/camera -type f | imagestore \
  --config ~/.imagestore.toml \
  --errors ~/errors.txt \
  --verbose
```

### Adjust Job Count Based on Storage Type

| Storage type | Recommended `--jobs` |
|---|---|
| NVMe SSD | default (all cores) |
| SATA SSD | default |
| Spinning HDD | 4–8 |
| Slow NAS | 2–4 |

Overloading a spinning drive with too much concurrency causes seek thrash and actually slows the import.

## Scripted / Automation Usage

`imagestore` is designed for scripted use:

```bash
#!/bin/bash
set -euo pipefail

find /media/camera -type f \
  | imagestore \
      --config /etc/imager.toml \
      --errors /var/log/imager-errors.txt \
      --quiet

if [[ $? -eq 2 ]]; then
    echo "Some files failed — check /var/log/imager-errors.txt" >&2
    exit 1
fi
```

The `--quiet` flag suppresses all non-error output, making the script output clean. The exit code tells you whether any failures occurred.

## Relationship to `libimager`

`imagestore` is a thin shell over `libimager`. It reads file paths from stdin and calls `lib.addFile(path)` (or `lib.validateOnlyFile(path, filename, onStage)` in dry-run mode) for each path. All validation, hashing, deduplication, and storage logic lives in the library.

This means the metrics exposed via `lib.metrics()` accurately reflect what `imagestore` is doing.
