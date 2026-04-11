# imager_cli Demo Tool

`imager_cli` is a command-line tool that exercises the full `libimager` API interactively. It is useful for exploring a library instance, verifying that configuration is correct, managing tags, and doing one-off operations. It is not intended as a production batch import tool — use `imagestore` for that.

## Synopsis

```
imager_cli [--metrics] <config.toml> <command> [args...]
```

The first positional argument is always the path to the TOML configuration file. The second is the command name. Additional arguments are command-specific.

The optional `--metrics` flag causes the tool to print collected metrics to stderr when it exits. This is useful for observing what one operation costs.

## Commands

### `add` — Add a File

```
imager_cli <config.toml> add <file>
```

Reads `<file>` from disk, validates it, and ingests it. Prints the assigned SHA256 id on success.

```bash
imager_cli ~/imager.toml add /path/to/photo.jpg
# Added: a1b2c3d4e5f6...
```

---

### `get` — Show Image Metadata

```
imager_cli <config.toml> get <id>
```

Prints tab-separated metadata for the image with the given SHA256 id.

Output columns: `id  name  size  ext  tags` (tags are comma-separated).

```bash
imager_cli ~/imager.toml get a1b2c3d4e5f6...
# id    name         size   ext    tags
# a1b2  photo.jpg    3145728 .jpg  vacation,2024
```

---

### `list` — List All Images

```
imager_cli <config.toml> list [--offset N] [--limit N]
```

Lists all images in the library, paginated. Default page size is 50.

```bash
# First page
imager_cli ~/imager.toml list

# Second page of 100
imager_cli ~/imager.toml list --offset 100 --limit 100
```

Output is tab-separated with column headers: `id  name  size  ext  tags`.

---

### `delete` — Delete an Image

```
imager_cli <config.toml> delete <id>
```

Removes the image and all its associated AAE sidecar files from storage and all databases.

```bash
imager_cli ~/imager.toml delete a1b2c3d4e5f6...
# Deleted: a1b2c3d4e5f6...
```

---

### `tag` — Add a Tag to an Image

```
imager_cli <config.toml> tag <id> <tag>
```

Creates the tag if it does not already exist, then associates it with the image.

```bash
imager_cli ~/imager.toml tag a1b2c3d4... vacation
# Tagged
```

---

### `untag` — Remove a Tag from an Image

```
imager_cli <config.toml> untag <id> <tag>
```

Removes the association between the image and the tag. The tag itself is not deleted.

```bash
imager_cli ~/imager.toml untag a1b2c3d4... vacation
# Untagged
```

---

### `tags` — List All Tags

```
imager_cli <config.toml> tags [--offset N] [--limit N]
```

Lists all tags in the library, one per line.

```bash
imager_cli ~/imager.toml tags
# beach
# family
# vacation
# 2024
```

---

### `search` — Find Images by Tags

```
imager_cli <config.toml> search <tag> [<tag>...] [--offset N] [--limit N]
```

Finds all images that have **all** of the listed tags (AND semantics). Multiple tags narrow the results.

```bash
# Images tagged "vacation"
imager_cli ~/imager.toml search vacation

# Images tagged both "vacation" and "2024"
imager_cli ~/imager.toml search vacation 2024

# Second page
imager_cli ~/imager.toml search vacation 2024 --offset 50 --limit 50
```

Output is tab-separated: `id  name  size  ext  tags`.

---

### `count` — Total Image Count

```
imager_cli <config.toml> count
```

Prints the total number of images stored in the library.

```bash
imager_cli ~/imager.toml count
# 14832 image(s)
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Usage error or configuration failure |
| `2` | Operation failed (file not found, broken file, etc.) |

---

## The `--metrics` Flag

When `--metrics` is specified, the tool prints a metrics snapshot to stderr after the command completes. This shows what the underlying library did, including timing histograms for validation, hashing, storage writes, and database inserts.

```bash
imager_cli --metrics ~/imager.toml add /path/to/photo.jpg 2>metrics.txt
```

The metrics output format is:

```
=== Pipeline Timings ===

addimage_total  count=1  avg=12.4ms  p50=12ms  p95=13ms  p99=13ms
validate        count=1  avg=3.1ms   p50=3ms   ...
hash            count=1  avg=1.2ms   ...
dedup_check     count=1  avg=0.3ms   ...
storage_write   count=1  avg=5.8ms   ...
db_insert       count=1  avg=2.1ms   ...
...
```

---

## Practical Workflows

### Verify a Configuration is Working

```bash
# Add one file to confirm the config is valid and storage is writable
imager_cli ~/imager.toml add /tmp/test.jpg
# Then delete it
imager_cli ~/imager.toml delete <id>
```

### Tag a Set of Files by Script

```bash
#!/bin/bash
CONFIG=~/imager.toml

while IFS= read -r id; do
    imager_cli "$CONFIG" tag "$id" "2024"
    imager_cli "$CONFIG" tag "$id" "summer"
done < file-ids.txt
```

### Export a File List

```bash
imager_cli ~/imager.toml list --limit 10000 > all-files.tsv
```

### Search and Verify Results

```bash
# How many vacation photos from 2024?
imager_cli ~/imager.toml search vacation 2024 --limit 1000 | wc -l
```

---

## Comparison with `imagestore`

| Feature | `imager_cli` | `imagestore` |
|---------|-------------|--------------|
| Primary purpose | Interactive exploration | Batch import |
| Input | Command arguments | Standard input |
| Concurrency | Single-threaded | Bounded parallel jobs |
| Progress reporting | None | Yes (with `--graph`) |
| Error file | No | Yes (`--errors`) |
| Dry run | No | Yes (`--dry-run`) |
| All API operations | Yes | Add only |

Use `imager_cli` for exploration, debugging, and one-off management tasks. Use `imagestore` for importing collections.
