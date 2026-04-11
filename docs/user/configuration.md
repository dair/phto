# Configuration Guide

Imager is configured through a single TOML file. This file is read once at startup; changes take effect only after a restart. This document covers every configuration option, explains the semantics of multi-target setups, and provides ready-to-use examples.

## File Format

The configuration file uses [TOML](https://toml.io/) syntax. The only top-level element is an array of `[[targets]]` tables. Each target defines one storage location.

```toml
[[targets]]
root     = "/path/to/storage/root"
database = "/path/to/imager.db"
```

At least one `[[targets]]` entry is required. The parser throws a `std::runtime_error` on startup if the file is missing, malformed, or contains no targets.

## Target Fields

### `root` (required)

The directory where image and video files are stored on disk. Imager creates a two-level shard hierarchy inside this directory:

```
<root>/
  a1/
    a1b2c3...d4.jpg
  ff/
    ffee12...ab.heic
```

The shard prefix is the first two hex characters of the file's SHA256 hash. This keeps individual directories manageable even with millions of files.

The directory must exist before starting Imager. Imager will not create it.

### `database` (required)

The path to the SQLite database file for this target. Each target has its own independent database. Imager creates the file and initializes the schema automatically if it does not already exist.

The directory containing the database file must exist.

## Example Configurations

### Minimal Single-Root Setup

```toml
[[targets]]
root     = "/home/alice/photos"
database = "/home/alice/photos/imager.db"
```

This is the simplest setup: files and the database live together under the same directory.

### Recommended: Separate Database Location

Keep the database on a path that is easy to back up independently:

```toml
[[targets]]
root     = "/data/photos"
database = "/var/lib/imager/imager.db"
```

### Recommended: Two-Root Redundancy

With two targets, every file is written to both roots atomically. If one write fails, both are rolled back. This protects against a single-drive failure without requiring a RAID array.

```toml
[[targets]]
root     = "/mnt/disk1/photos"
database = "/mnt/disk1/imager.db"

[[targets]]
root     = "/mnt/disk2/photos"
database = "/mnt/disk2/imager.db"
```

Reads always come from the first target. The second target is a hot standby.

### Three or More Roots

You can add as many targets as you have drives. All targets are written in parallel on each ingestion:

```toml
[[targets]]
root     = "/mnt/primary/photos"
database = "/mnt/primary/imager.db"

[[targets]]
root     = "/mnt/backup1/photos"
database = "/mnt/backup1/imager.db"

[[targets]]
root     = "/mnt/offsite-backup/photos"
database = "/mnt/offsite-backup/imager.db"
```

### NAS / Network Storage

Network paths work exactly like local paths. Mount the share first and then reference the mount point:

```toml
[[targets]]
root     = "/mnt/nas/photos"
database = "/mnt/nas/imager.db"
```

If the network path is unavailable when Imager starts, the write to that target will fail during ingestion. With multi-root setups, this triggers a rollback for that file on all roots, and the ingestion returns `StorageError`.

## Multi-Root Semantics

When you configure more than one target, Imager enforces **all-or-nothing writes**:

1. The file is validated and hashed once.
2. Imager writes the file to all storage roots in parallel.
3. Imager inserts the metadata record into all databases in parallel.
4. If any write or insert fails, all successful writes are rolled back and the insertion returns an error.

This guarantees that all targets stay in sync. There is no partial-success state where one root has a file and another does not.

**Reads** always come from the first target in the configuration file. The other targets are never used for reads; they exist solely for redundancy. If the first target becomes unavailable, you must either bring it back online or edit the configuration to promote a different target to the first position and restart.

## Where to Place the Configuration File

`imagestore` looks for its configuration file in this order:

1. The path given to `--config PATH`
2. `~/.imagestore.toml` (default)

`imager_cli` requires the path as the first positional argument.

When embedding `libimager` in your own application, pass the path to `config::loadConfig()`:

```cpp
auto cfg = config::loadConfig("/etc/myapp/imager.toml");
```

## Validating Your Configuration

There is no separate validate-config command, but you can use `imagestore` in dry-run mode to confirm the configuration loads correctly without writing anything:

```bash
echo "" | imagestore --config /path/to/imager.toml --dry-run
```

If the config file is missing or malformed, `imagestore` prints an error and exits with code 1.

## Common Configuration Mistakes

**Using a root path that does not exist.** Imager will fail on the first write, not at startup, which can be confusing. Pre-create all root directories before starting.

**Pointing multiple targets at the same root directory.** This is technically accepted by the parser but writes the file twice to the same location and effectively creates duplicate database records. Use different directories for each target.

**Using relative paths.** Relative paths in the config file are resolved relative to the working directory at startup, which can vary. Always use absolute paths.

**Sharing a database file between targets.** Each target must have its own database file. Sharing one database file between targets causes SQLite locking errors and data corruption.

## Configuration and Metrics

Configuration has no impact on what metrics are collected. Metrics are always-on and are exposed through `Imager::metrics()`. The number of targets does affect the multi-root write histograms (`storage_write_root`, `db_insert_single`) which track per-target latency independently.

## Hot Reload

Configuration is **not** hot-reloaded. If you edit the configuration file while Imager is running, the changes are ignored until the application is restarted.

This is by design: changing the number or order of targets while files are being ingested could leave the storage and databases in an inconsistent state.
