# Storage and Data Model

This document describes how Imager organizes files on disk, what information is stored in the database, and the full mechanics of AAE sidecar file handling including orphan resolution and cascade deletion.

## File Identity: SHA256 Hashing

Every file in Imager is identified by its **SHA256 content hash**, computed over the entire file. This hash serves as:

- The file's unique identifier (the `id` field in API responses)
- The filename on disk (plus the original extension)
- The deduplication key — if two files have the same SHA256, they are considered identical

SHA256 is computed using OpenSSL. The result is a 64-character lowercase hex string, for example:

```
a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2
```

### Why SHA256?

SHA256 provides a 256-bit (32-byte) hash space. The probability of a collision in a collection of 10 billion files is approximately 10^-58, which is negligible for any practical photo library. SHA256 is also fast enough that it does not become a bottleneck in the ingestion pipeline when hashing runs in parallel with validation.

---

## On-Disk Storage Layout

Within each storage root, files are arranged in a two-level directory hierarchy based on the first two hex characters of their SHA256 hash:

```
<root>/
  a1/
    a1b2c3d4...f0.jpg
    a1f7aa11...32.heic
  ff/
    ffee1234...ab.mov
  00/
    00abcdef...12.nef
```

This sharding scheme keeps directory listings manageable. With 256 possible two-character prefixes, a collection of 1 million files results in an average of about 3900 files per shard directory. Even at 10 million files, each directory holds around 39000 entries, which is within the comfortable range for most filesystems.

The filename within the shard directory is `<sha256>.<ext>`, where `<ext>` is the lowercase extension with a leading dot (for example `.jpg`, `.heic`, `.mov`). The extension is preserved so that tools that read the storage root directly (without going through the library) can identify the file type.

### Example: Adding a File

Given a file `vacation/IMG_0042.JPG` with SHA256 `a1b2c3...f4`:

1. The shard directory is computed: `a1` (first two hex chars)
2. Imager creates `<root>/a1/` if it does not exist
3. The file is written to `<root>/a1/a1b2c3...f4.jpg`
4. A database record is inserted: `id=a1b2c3...f4, name=IMG_0042.JPG, size=..., ext=.jpg`

---

## Database Schema

Each storage root has its own SQLite database. All databases are kept in sync — every write goes to all databases in parallel. The schema is created automatically on first use.

### `file` Table

The primary table. One row per stored file.

```sql
CREATE TABLE IF NOT EXISTS file (
    id   TEXT PRIMARY KEY NOT NULL,  -- SHA256 hex (64 chars)
    name TEXT NOT NULL,              -- Original filename (basename only, no directory)
    size INTEGER NOT NULL,           -- File size in bytes
    ext  TEXT NOT NULL               -- Lowercase extension with dot, e.g. ".jpg"
);
```

### `tag` Table

One row per tag. Tags are simple string labels.

```sql
CREATE TABLE IF NOT EXISTS tag (
    name TEXT PRIMARY KEY NOT NULL
);
```

### `file_tag` Table

The many-to-many association between files and tags.

```sql
CREATE TABLE IF NOT EXISTS file_tag (
    file_id  TEXT NOT NULL REFERENCES file(id) ON DELETE CASCADE,
    tag_name TEXT NOT NULL REFERENCES tag(name) ON DELETE CASCADE,
    PRIMARY KEY (file_id, tag_name)
);
```

Deleting a file automatically removes all its tag associations via the `ON DELETE CASCADE` constraint.

### `original_name` Table

Tracks the source directory and base filename (without extension) of every ingested file. This table powers AAE sidecar pairing.

```sql
CREATE TABLE IF NOT EXISTS original_name (
    source_dir TEXT NOT NULL,   -- Directory portion of the original path, lowercased
    base_name  TEXT NOT NULL,   -- Filename without extension, lowercased
    file_id    TEXT NOT NULL REFERENCES file(id) ON DELETE CASCADE,
    PRIMARY KEY (source_dir, base_name, file_id)
);

CREATE INDEX IF NOT EXISTS idx_original_name_pairing
    ON original_name(source_dir, base_name);
```

**Every file** gets an entry here, not just sidecars. This makes parent lookups O(1) regardless of library size.

The `source_dir` and `base_name` values are derived from the `filename` parameter passed to `addImage` / `addFile`:

- For `"vacation/IMG_1234.JPG"`: `source_dir = "vacation"`, `base_name = "img_1234"`
- For `"/photos/vacation/IMG_1234.JPG"`: `source_dir = "/photos/vacation"`, `base_name = "img_1234"`
- For `"IMG_1234.JPG"` (bare, no path): `source_dir = ""`, `base_name = "img_1234"`

All matching is case-insensitive (values are stored lowercase).

### `file_companion` Table

Tracks sidecar relationships between AAE files and their parent images.

```sql
CREATE TABLE IF NOT EXISTS file_companion (
    file_id    TEXT PRIMARY KEY NOT NULL REFERENCES file(id) ON DELETE CASCADE,
    parent_id  TEXT REFERENCES file(id) ON DELETE SET NULL,
    storage_id TEXT NOT NULL
);
```

| Column | Description |
|--------|-------------|
| `file_id` | The sidecar file's own SHA256 hash |
| `parent_id` | The parent file's SHA256 hash, or `NULL` if the parent has not yet been added (orphan state) |
| `storage_id` | The hash used for the on-disk filename. Equals `parent_id` when the parent is known; equals `file_id` when orphaned |

---

## AAE Sidecars

### What is an AAE File?

AAE (Apple Adjustment Expression) files are XML-based plist files created by Apple Photos on iOS and macOS. They record non-destructive edits — crops, filters, color adjustments — applied to a photo or video. Key characteristics:

- Always paired: `IMG_1234.AAE` corresponds to `IMG_1234.JPG` (or `.HEIC`, `.MOV`, etc.)
- Small: typically 1–5 KB
- Not media: they contain edit metadata, not image data

### The Pairing Problem

If an AAE file were stored by its own content hash (like any other file), the disk filename would be unrelated to its parent image's filename. To preserve the pairing relationship on disk, Imager stores the AAE with the **parent image's hash** as the filename prefix:

```
<root>/
  a1/
    a1b2c3...f4.jpg    ← the JPEG
    a1b2c3...f4.aae    ← its AAE, stored with the same hash prefix
```

A consumer of the storage root can reconstruct the pair by matching filenames: any two files in the same shard with the same hash prefix are paired.

### How Pairing Works

Pairing uses the **source directory and base filename** as a key, not just the base filename. This prevents cross-pairing when two cameras produce files with the same name in different directories.

Given `vacation/IMG_1234.JPG` and `vacation/IMG_1234.AAE`:
- The pairing key for the JPEG is `(source_dir="vacation", base_name="img_1234")`
- The pairing key for the AAE is also `(source_dir="vacation", base_name="img_1234")`
- They match — the AAE is stored with the JPEG's hash as its filename

Given `camera_a/IMG_0001.JPG` and `camera_b/IMG_0001.AAE`:
- JPEG key: `(source_dir="camera_a", base_name="img_0001")`
- AAE key: `(source_dir="camera_b", base_name="img_0001")`
- Different `source_dir` — they do not match, the AAE remains an orphan

### Scenario A: Parent Exists When AAE is Added

This is the common case when importing an already-organized directory.

1. The parent JPEG is imported first, as normal.
2. The AAE is imported with a path-bearing filename: `addFile("/photos/vacation/IMG_1234.AAE")`.
3. The library looks up `(source_dir="vacation", base_name="img_1234")` in `original_name`.
4. It finds the parent JPEG's id: `a1b2c3...f4`.
5. The AAE is stored on disk as `<root>/a1/a1b2c3...f4.aae`.
6. A `file_companion` record is inserted: `file_id=aae_hash, parent_id=a1b2c3...f4, storage_id=a1b2c3...f4`.

### Scenario B: AAE Arrives Before Its Parent (Orphan)

This happens when files arrive in alphabetical order or the parent is imported later.

1. The AAE is imported first: `addFile("/photos/vacation/IMG_1234.AAE")`.
2. No parent found in `original_name` — the AAE becomes an orphan.
3. The AAE is stored temporarily with its own hash: `<root>/aa/aabb...ee.aae`.
4. A `file_companion` record is inserted: `file_id=aae_hash, parent_id=NULL, storage_id=aae_hash`.

When the parent image is later imported:

1. The JPEG is processed normally and stored.
2. The library inserts `(source_dir="vacation", base_name="img_1234", file_id=jpeg_hash)` into `original_name`.
3. It searches `file_companion` for orphan sidecars with a matching pairing key.
4. It finds the orphaned AAE.
5. The AAE file is **relocated** on disk: moved from `<root>/aa/aabb...ee.aae` to `<root>/a1/a1b2c3...f4.aae`.
6. The `file_companion` record is updated: `parent_id=a1b2c3...f4, storage_id=a1b2c3...f4`.

Relocation is efficient because AAE files are small (1–5 KB) and the move is a filesystem rename within the same storage root, which is atomic.

### Cascade Deletion

When a parent image is deleted, all its associated AAE sidecar files are automatically deleted:

1. `deleteImage(parent_hash)` is called.
2. The library queries `file_companion` for all entries where `parent_id = parent_hash`.
3. Each sidecar is removed from all storage roots using its `storage_id` for the filesystem path.
4. Each sidecar's database record is deleted (which cascades to `file_companion` and `original_name`).
5. The parent image itself is then removed from storage and the database.

This means you do not need to explicitly delete sidecar files — they follow their parent.

### Ambiguous Sidecars

If the same source directory contains both `IMG_1234.JPG` and `IMG_1234.HEIC`, an incoming `IMG_1234.AAE` has two potential parents. In this case:

1. Imager prefers the image over the video (JPG/HEIC over MOV/MP4).
2. If both candidates are images (JPG and HEIC), the pairing is ambiguous and Imager returns `AmbiguousSidecar`.

The `AmbiguousSidecar` error means the user must resolve the ambiguity externally (for example, by removing one of the conflicting parent files before re-importing the AAE).

### Retrieving a Sidecar File

`getImage(aae_id)` and `getImageData(aae_id)` work normally using the sidecar's own SHA256 hash as the id. Internally, the library looks up the `storage_id` from `file_companion` and uses that to compute the correct on-disk path.

---

## Multi-Root Storage

When multiple targets are configured, every file write goes to all roots in parallel using coroutines:

```
addFile("/photo.jpg")
  → validate + hash
  → write to root1 (concurrent)
  → write to root2 (concurrent)
  → insert into db1 (concurrent)
  → insert into db2 (concurrent)
```

If any write or database insert fails, all successful writes are rolled back:

- Files that were written to some roots but not others are deleted from the successful roots.
- Database inserts that succeeded are reverted.
- The overall `addFile` call returns the error from the failed operation.

This all-or-nothing guarantee means all storage roots are always in sync with each other.

**Read operations** always use the first configured target. The other targets are hot standbys and are never used for reads in normal operation. If the first root is unavailable and `getImageData` is called, it retries the remaining roots in order before returning an empty blob.

---

## Deduplication

Deduplication is based entirely on SHA256. If a file with the same hash already exists in the database, `addFile` returns `DuplicateFile` without writing to storage. The `result.id` field is populated with the existing file's id.

Deduplication is checked inside the write mutex to prevent two concurrent threads from inserting the same file simultaneously. This means that even under heavy concurrent load, exactly one copy of any given file is ever stored.

**Note on storage_id vs file_id for sidecars:** Deduplication for AAE files still uses the sidecar's own content hash (`file_id`), not the `storage_id`. If the same AAE edit is imported twice (same content), the second import returns `DuplicateFile` based on the AAE's own hash.

---

## Disk Space Considerations

Files are stored once per storage root (no intra-root duplication). With two storage roots, each file occupies space on both drives — this is the cost of redundancy.

The database is typically small relative to the media files. For a collection of 100,000 files, expect the database to be in the range of 50–200 MB depending on tag usage.

The shard directory overhead is negligible: 256 directories of a few hundred bytes each amounts to roughly 100 KB per storage root regardless of library size.
