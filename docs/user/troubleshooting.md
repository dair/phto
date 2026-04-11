# Troubleshooting

This guide covers common problems, their causes, and how to resolve them.

---

## Build Problems

### CMake cannot find a system library

**Symptom:**

```
CMake Error: Could not find a package configuration file provided by "libheif"
```

or

```
-- Could NOT find JPEG (missing: JPEG_LIBRARY JPEG_INCLUDE_DIR)
```

**Cause:** A required system library is not installed or its development headers are missing.

**Resolution:** Install the missing package. On Debian/Ubuntu:

```bash
apt-get install libsqlite3-dev libjpeg-dev libpng-dev libheif-dev \
                libraw-dev libssl-dev \
                libavformat-dev libavcodec-dev libavutil-dev
```

Then re-run `cmake --preset default`.

---

### Build fails with "C++ standard not supported"

**Symptom:**

```
error: 'std::jthread' is not a member of 'std'
error: coroutines require '-fcoroutines-ts'
```

**Cause:** The compiler is too old to support C++23 features.

**Resolution:**

- For Clang: upgrade to Clang 17 or newer.
- For GCC: upgrade to GCC 13 or newer.

Check your compiler version:

```bash
clang++ --version
g++ --version
```

---

### Tests not built / CTest reports 0 tests

**Symptom:** `ctest --preset default` finishes immediately with "No tests were found."

**Cause:** CppUnit was not installed, so test targets were silently skipped.

**Resolution:**

```bash
apt-get install libcppunit-dev
cmake --preset default
cmake --build --preset default
ctest --preset default
```

---

## Configuration Problems

### `Config error: unable to open file`

**Cause:** The configuration file path is wrong, or the file does not exist.

**Resolution:** Check the path. `imagestore` defaults to `~/.imagestore.toml`. To use a different path:

```bash
imagestore --config /path/to/my-imager.toml ...
```

---

### `Config error: no [[targets]] entries`

**Cause:** The configuration file exists but contains no `[[targets]]` sections.

**Resolution:** Add at least one target to the config:

```toml
[[targets]]
root     = "/path/to/photos"
database = "/path/to/imager.db"
```

---

### `Config error: missing field 'root'` or `missing field 'database'`

**Cause:** A `[[targets]]` block is incomplete.

**Resolution:** Verify each target block has both `root` and `database` fields. The field names are case-sensitive.

---

## Ingestion Errors

### `UnsupportedFormat` — file extension not recognized

**Cause:** The file has an extension that Imager does not have a validator for.

**Accepted extensions:** `.jpg`, `.jpeg`, `.png`, `.heic`, `.heif`, `.nef`, `.mov`, `.mp4`, `.aae`

**Resolution:** If you want to ingest files of that type, pre-filter your `find` output to only pass recognized extensions:

```bash
find /media -type f \( -iname "*.jpg" -o -iname "*.heic" -o -iname "*.mov" \) \
  | imagestore --config ~/.imagestore.toml
```

Files with unsupported extensions are never treated as data loss — they are simply rejected cleanly.

---

### `BrokenFile` — validation failed

**Cause:** The file content is corrupt, truncated, or not actually the format the extension suggests.

**Common cases:**

- A `.jpg` file that is actually a renamed PNG or other format
- A partially downloaded file
- A file whose header was written correctly but whose data is corrupt
- A video file where the first frame cannot be decoded

**Resolution:**

1. Check the error message for details: `imagestore --verbose` prints the full error per file.
2. Open the file in a viewer to confirm it is actually corrupted.
3. If it is corrupt, the file has a genuine data problem that Imager cannot fix. Keep it in the error file for manual review.
4. If the file opens fine in a viewer but Imager rejects it, it may be an edge case. Check whether the file uses a subformat or encoding that the underlying library (libjpeg, libheif, etc.) does not support.

---

### `DuplicateFile` — already in the library

This is not an error. `DuplicateFile` means Imager has already stored an identical copy of this file (same SHA256 hash). The second import is silently skipped.

`imagestore` does not write duplicate paths to the error file and does not count them as failures. Exit code `0` is returned even if all files in a batch are duplicates.

If you need to find out which existing entry a file corresponds to, the id is returned in `result.id`.

---

### `StorageError` — filesystem I/O failure

**Common causes:**

- The storage root directory does not exist or is not writable
- The disk is full
- A network storage path became unavailable during the import
- Permissions prevent creating the shard directory or writing the file

**Resolution:**

1. Check that the `root` directories in your config exist and are writable:
   ```bash
   ls -la /mnt/disk1/photos
   touch /mnt/disk1/photos/.test && rm /mnt/disk1/photos/.test
   ```
2. Check disk space:
   ```bash
   df -h /mnt/disk1
   ```
3. For network paths, verify connectivity before starting an import.

---

### `DatabaseError` — SQLite failure

**Common causes:**

- The directory containing the `.db` file does not exist
- The database file is corrupted
- Two processes are trying to write to the same database file simultaneously

**Resolution:**

1. Ensure the parent directory of the database file exists.
2. If the database file is corrupted (SQLite reports "file is not a database" or similar), the database needs to be rebuilt. If you have a secondary target, the data is still accessible there. Delete the corrupted database file and restart Imager — it will create a fresh empty database. You will then need to re-import files to that target.
3. Do not run multiple `Imager` instances pointing at the same database file simultaneously. Each process should have its own `Imager` instance and its own config.

---

### `AmbiguousSidecar` — multiple potential parents for an AAE

**Cause:** An `.aae` file was imported, and the same source directory contains two media files with the same base name and different extensions (for example, `IMG_1234.JPG` and `IMG_1234.HEIC`). Imager cannot determine which one to attach the sidecar to.

**Resolution:**

1. Identify which file is the actual parent of the edit (usually the JPEG for Apple Photos adjustments).
2. Remove or rename the unwanted conflicting file from the source directory.
3. Re-import the AAE file.

Note: if one is an image and one is a video (for example `IMG_1234.JPG` and `IMG_1234.MOV`, which is an Apple Live Photo), Imager automatically prefers the image file and does not return `AmbiguousSidecar`.

---

## `imagestore` Problems

### No progress output / tool hangs

**Symptom:** `imagestore` starts but prints nothing and appears to hang.

**Cause:** Standard input has no data. If you are not piping from `find` or another source, `imagestore` blocks waiting for input.

**Resolution:** Always pipe input:

```bash
find /media/camera -type f | imagestore --config ~/.imagestore.toml
```

---

### Exit code 2 with no verbose output

**Cause:** Files failed but `--verbose` is off, so errors are not shown.

**Resolution:** Re-run with `--verbose` to see per-file error details:

```bash
find /media/camera -type f | imagestore --config ~/.imagestore.toml --verbose 2>&1 | grep ERR
```

Or use `--errors` to capture failed paths to a file.

---

### `Cannot open error file`

**Cause:** The directory for the error file does not exist.

**Resolution:** Create the directory first:

```bash
mkdir -p ~/import-logs
imagestore --config ~/.imagestore.toml --errors ~/import-logs/errors.txt
```

---

### Import is very slow

**Possible causes and solutions:**

1. **Spinning hard drive with too many jobs.** Reduce concurrency: `--jobs 4`
2. **Large video files.** MOV/MP4 validation performs a trial decode of the first frame, which can be slow for large files. This is unavoidable.
3. **Network storage.** NAS round-trip latency adds up. Reduce concurrency and consider copying files locally before importing.
4. **Single storage root.** Adding a second root doubles write I/O. If you only need redundancy on completion, import to one root first and then sync.

Use `--graph` to see which pipeline stage is the bottleneck.

---

### `--graph` shows nothing / falls back to normal mode

**Cause:** `--graph` requires stderr to be a TTY. If you have redirected stderr, the graph mode falls back silently.

**Resolution:** Do not redirect stderr when using `--graph`:

```bash
# Wrong
find . -type f | imagestore --config ~/.imagestore.toml --graph 2>log.txt

# Right: use verbose to log per-file, graph for visual display
find . -type f | imagestore --config ~/.imagestore.toml --graph
```

---

## Performance Issues

### Hash computation is slow

SHA256 computation speed depends heavily on hardware. On modern x86_64 processors, OpenSSL uses AES-NI hardware acceleration to speed up SHA256. If your processor does not support AES-NI, hash speed will be lower.

For a CPU without AES-NI, hashing a 10 MB JPEG may take 10–20 ms. With AES-NI, the same file hashes in under 5 ms.

Check whether OpenSSL is using hardware acceleration:

```bash
openssl speed sha256
```

---

### Dedup check is slow

The dedup check is a single SQLite `EXISTS` query with an indexed lookup. It should be fast (sub-millisecond) for any library size. If it is slow:

1. Check that the database file is on a local, fast drive (not spinning HDD or NAS).
2. Check that SQLite's WAL mode is active — the library enables it at connection time, but if the database file was created by a different tool it may be in journal mode.

---

### Memory usage grows during large imports

Each in-flight file is held as a `Blob` in memory until it completes all pipeline stages. With `--jobs 32` and an average file size of 5 MB, the peak memory usage is roughly `32 * 5 MB = 160 MB` just for in-flight data.

If memory is a concern, reduce `--jobs`:

```bash
imagestore --config ~/.imagestore.toml --jobs 8
```

---

## AAE Sidecar Problems

### AAE file stored as orphan even though the parent exists

**Cause:** The AAE was imported with a different `source_dir` than the parent. For example, the parent was imported as `"vacation/IMG_1234.JPG"` but the AAE was imported as `"IMG_1234.AAE"` (no path prefix).

**Resolution:** Always pass path-bearing filenames when importing. When using `find`, this happens automatically because `find` outputs the full path:

```bash
# Good: find includes directory path
find /photos/vacation -type f | imagestore --config ~/.imagestore.toml
```

If you imported with bare filenames and have orphan AAE files, you would need to re-import them with correct path context.

---

### AAE file was deleted along with its parent unexpectedly

**Cause:** Deleting a parent image via `deleteImage` also deletes all associated sidecar files. This is by design.

**Resolution:** If you want to keep the edit metadata, export the AAE file first using `getImageData(aae_id)` before deleting the parent.

---

## Recovering from a Crashed Import

If `imagestore` is killed mid-import (power loss, OOM, signal), files that were in flight may have been partially written.

**What state the library is in:**

The write protocol is all-or-nothing per file, but "all-or-nothing" means either the file is fully written to all roots and all databases, or the write is rolled back. A mid-flight crash means:

- Files that completed successfully before the crash are intact.
- Files that were in-flight are in an unknown state. In the worst case, a file was written to storage but the database insert was not completed (crash between step 6 and step 7 in the pipeline). This file would be an orphan on disk with no database record.

**Recovering:**

1. Re-run the import. Files that were fully stored will be detected as duplicates and skipped quickly.
2. If you used `--errors`, files that failed are in the error file and will be retried automatically.
3. Orphaned disk files (written but no DB record) are harmless; they waste some disk space but do not interfere with the library's operation. They can be found by comparing the files in the storage root against the database, and removed manually.

---

## Getting More Diagnostic Information

### Enable Metrics Output

```bash
imager_cli --metrics ~/.imagestore.toml count
```

This runs a simple operation and then dumps all collected metrics, useful for confirming that the library is connecting to the right databases and storage roots.

### Check Database Directly

The SQLite databases can be queried directly with the `sqlite3` CLI:

```bash
sqlite3 /path/to/imager.db
sqlite> SELECT COUNT(*) FROM file;
sqlite> SELECT * FROM file LIMIT 5;
sqlite> SELECT name FROM tag;
sqlite> .quit
```

This is useful for verifying that records are actually being inserted.

### Check Storage Root Directly

```bash
# Count total files in the storage root
find /mnt/disk1/photos -type f | wc -l

# List recent files
find /mnt/disk1/photos -type f -newer /tmp/marker -ls
```
