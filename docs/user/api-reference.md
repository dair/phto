# Library API Reference

This document is the complete reference for `imager::Imager`, the main public facade of `libimager`. It covers all methods, supporting types, and error codes.

## Including the Library

```cpp
#include <config/Config.h>   // config::AppConfig, config::loadConfig()
#include <imager/Imager.h>   // imager::Imager
#include <imager/Types.h>    // imager::AddResult, imager::ImageInfo, imager::ErrorCode, imager::Blob
```

`<imager/Types.h>` is an umbrella header that includes all supporting types. You can also include individual type headers:

```cpp
#include <imager/types/AddResult.h>
#include <imager/types/ErrorCode.h>
#include <imager/types/ImageInfo.h>
#include <imager/types/Blob.h>
```

## Configuration

Before constructing `Imager` you must parse a configuration file.

```cpp
namespace config {
    AppConfig loadConfig(const std::filesystem::path& configPath);
}
```

`loadConfig` throws `std::runtime_error` if:
- the file does not exist or cannot be read
- the TOML syntax is invalid
- no `[[targets]]` entries are present
- a target is missing a required field

The `AppConfig` struct is:

```cpp
struct TargetConfig {
    std::filesystem::path root;     // Storage root directory
    std::filesystem::path database; // SQLite database file path
};

struct AppConfig {
    std::vector<TargetConfig> targets;  // At least one required
};
```

## Imager Class

```cpp
namespace imager {
    class Imager {
    public:
        explicit Imager(const config::AppConfig& cfg);
        ~Imager();

        // Non-copyable, non-movable
        Imager(const Imager&) = delete;
        Imager& operator=(const Imager&) = delete;
        Imager(Imager&&) = delete;
        Imager& operator=(Imager&&) = delete;

        // ... methods described below ...
    };
}
```

### Construction

```cpp
explicit Imager(const config::AppConfig& cfg);
```

Opens or creates all databases specified in `cfg.targets`. If a database file does not exist, it is created and the schema is initialized. If any database cannot be opened, the constructor throws `std::runtime_error`.

The constructor also starts an internal thread pool used for all concurrent I/O. The pool size is determined automatically based on the number of targets and available hardware.

### Destruction

The destructor shuts down the thread pool, waits for all in-flight coroutines to complete, and closes all database connections. It is safe to call the destructor from any thread as long as no other thread is concurrently calling methods on the same instance.

---

## Core Ingestion Methods

### `addFile`

```cpp
AddResult addFile(
    const std::filesystem::path& path,
    const std::string& filename = ""
);
```

Reads a file from disk and runs it through the full ingestion pipeline: read, validate, hash, dedup check, multi-root write, multi-database insert.

**Parameters:**

- `path` — absolute or relative path to the file on disk. The I/O happens inside the library, making it observable via the `stage_read` and `file_read` metrics.
- `filename` — the display name stored in the database. If empty, the final component of `path` is used. This parameter accepts an optional path prefix (e.g., `"vacation/IMG_1234.JPG"`) for correct AAE sidecar pairing when importing from a directory hierarchy. See the [Storage and Data Model](storage.md#aae-sidecars) guide for details.

**Returns:** an `AddResult`. On success, `code` is `ErrorCode::Ok` and `id` is the SHA256 hex string (64 characters). On failure, `code` is a non-Ok error code and `message` contains a human-readable description.

**Example:**

```cpp
auto result = lib.addFile("/media/camera/IMG_0042.JPG");
if (result.code == imager::ErrorCode::Ok) {
    std::cout << "id: " << result.id << "\n";
}
```

---

### `addImage`

```cpp
AddResult addImage(const Blob& blob, const std::string& filename);
```

Ingests raw bytes from an in-memory buffer. Useful when you have already read or generated the file data yourself.

**Parameters:**

- `blob` — a frozen `Blob` containing the file data. The blob must have been frozen with `blob.freeze()` before passing it in.
- `filename` — the display name and extension hint. May include a path prefix for AAE pairing (e.g., `"vacation/IMG_1234.JPG"`).

**Returns:** same as `addFile`.

**Example:**

```cpp
// Read a file yourself
std::ifstream f("/path/to/photo.jpg", std::ios::binary);
std::vector<uint8_t> data{std::istreambuf_iterator<char>(f), {}};

imager::Blob blob = imager::Blob::fromVector(std::move(data));
auto result = lib.addImage(blob, "photo.jpg");
```

---

### `validateOnly`

```cpp
AddResult validateOnly(const Blob& blob, const std::string& filename);
```

Validates and hashes a file without writing anything to disk or any database. The dedup check is still performed: if the file is already in the database, `DuplicateFile` is returned.

This is the mode used by `imagestore --dry-run`. It is useful for pre-screening a batch before committing to storage, or for testing whether a file would be accepted.

**Returns:** same as `addFile`. `result.id` is populated even on `DuplicateFile` so the caller can identify which existing entry the file duplicates.

---

## Retrieval Methods

### `getImage`

```cpp
std::optional<ImageInfo> getImage(const std::string& id);
```

Returns metadata and tags for the file with the given SHA256 id.

**Parameters:**
- `id` — the 64-character SHA256 hex string returned by a previous `addFile` / `addImage` call.

**Returns:** `std::nullopt` if no file with that id exists; otherwise an `ImageInfo` struct.

**Example:**

```cpp
auto info = lib.getImage("a1b2c3d4...");
if (info) {
    std::cout << info->name << "  " << info->size << " bytes\n";
    for (const auto& tag : info->tags) {
        std::cout << "  tag: " << tag << "\n";
    }
}
```

---

### `getImagesByTags`

```cpp
std::vector<ImageInfo> getImagesByTags(
    const std::vector<std::string>& tags,
    uint32_t offset = 0,
    uint32_t limit  = 50
);
```

Returns all images that carry every tag in `tags` (AND semantics). Results are paginated.

**Parameters:**
- `tags` — one or more tag names. Only images that have ALL listed tags are returned. An empty `tags` vector returns no results (not all images).
- `offset` — zero-based index of the first result to return.
- `limit` — maximum number of results to return. Default is 50.

**Example:**

```cpp
// Get the first 20 images tagged both "vacation" and "2024"
auto results = lib.getImagesByTags({"vacation", "2024"}, 0, 20);
for (const auto& img : results) {
    std::cout << img.id << "  " << img.name << "\n";
}
```

---

### `listImages`

```cpp
std::vector<ImageInfo> listImages(uint32_t offset = 0, uint32_t limit = 50);
```

Returns all images, paginated. Tags are included in each `ImageInfo`.

---

### `getImageData`

```cpp
Blob getImageData(const std::string& id);
```

Reads the raw file bytes for an image, returning them as a frozen `Blob`.

Reads from the first configured storage root. If the read fails (e.g., drive unavailable), it automatically retries the remaining roots in order.

Returns an empty `Blob` if the file is not found or all reads fail.

---

### `imageCount`

```cpp
uint64_t imageCount();
```

Returns the total number of files stored in the database.

---

## Tag Management Methods

### `tagImage`

```cpp
ErrorCode tagImage(const std::string& id, const std::string& tag);
```

Associates a tag with an image. The tag must already exist; call `createTag` first if needed. Returns `FileNotFound` if the image does not exist, `DatabaseError` if the tag does not exist.

**Example:**

```cpp
lib.createTag("vacation");
lib.tagImage(photoId, "vacation");
```

---

### `untagImage`

```cpp
ErrorCode untagImage(const std::string& id, const std::string& tag);
```

Removes the association between an image and a tag. Returns `FileNotFound` if the image does not exist.

---

### `getImageTags`

```cpp
std::vector<std::string> getImageTags(const std::string& id);
```

Returns all tag names associated with the given image. Returns an empty vector if the image has no tags or does not exist.

---

### `createTag`

```cpp
ErrorCode createTag(const std::string& name);
```

Creates a new tag. If the tag already exists, returns `Ok` (idempotent). Tags are simple string labels — any non-empty string is valid.

---

### `deleteTag`

```cpp
ErrorCode deleteTag(const std::string& name);
```

Deletes a tag and removes all its associations with images. The images themselves are not affected. Returns `FileNotFound` if the tag does not exist.

---

### `listTags`

```cpp
std::vector<std::string> listTags(uint32_t offset = 0, uint32_t limit = 50);
```

Returns all known tag names, paginated.

---

## Deletion

### `deleteImage`

```cpp
ErrorCode deleteImage(const std::string& id);
```

Removes an image from all storage roots and all databases. If the image has associated AAE sidecar files, they are also deleted from storage and the database (cascade delete).

Returns `FileNotFound` if no image with that id exists. Returns `StorageError` if a filesystem deletion fails.

---

## Metrics

### `metrics`

```cpp
const metrics::Metrics& metrics() const noexcept;
```

Returns a reference to the internal `Metrics` instance owned by this `Imager`. The reference is valid for the lifetime of the `Imager` object.

See the [Metrics and Monitoring](metrics.md) guide for a description of all available metrics.

---

## Supporting Types

### `ErrorCode`

```cpp
enum class ErrorCode {
    Ok,                // Success
    BrokenFile,        // Validation failed; the file is corrupt or malformed
    DuplicateFile,     // A file with this SHA256 hash already exists
    UnsupportedFormat, // The file extension is not recognized
    FileNotFound,      // The requested id does not exist in the database
    StorageError,      // Filesystem I/O failure (read or write)
    AmbiguousSidecar,  // AAE sidecar matches multiple potential parent files
    DatabaseError,     // SQLite error
    ConfigError,       // Configuration problem
};
```

**Handling duplicates correctly:** `DuplicateFile` is not an error in most workflows. It means the exact same bytes are already stored. The id of the existing entry is returned in `result.id` so callers can retrieve it if needed. `imagestore` counts duplicates separately and does not append them to the error file.

---

### `AddResult`

```cpp
struct AddResult {
    ErrorCode code{ErrorCode::Ok};
    std::string id;      // SHA256 hex string (64 chars); set on Ok and DuplicateFile
    std::string message; // Human-readable error description; set on non-Ok
};
```

---

### `ImageInfo`

```cpp
struct ImageInfo {
    std::string id;                   // SHA256 hex string (64 chars)
    std::string name;                 // Original filename (basename only)
    uint64_t    size{0};              // File size in bytes
    std::string ext;                  // Lowercase extension with leading dot (e.g., ".jpg")
    std::vector<std::string> tags;    // All tags associated with this image
};
```

---

### `Blob`

`Blob` is a shared-ownership binary buffer defined in `<imager/types/Blob.h>`. It is used to pass raw file data into and out of `addImage`, `validateOnly`, and `getImageData`.

```cpp
// Construct an empty blob for manual filling
imager::Blob blob(fileSize);
stream.read(reinterpret_cast<char*>(blob.writableData()), fileSize);
blob.freeze();   // mark immutable before sharing

// Adopt an existing vector (no copy)
imager::Blob blob = imager::Blob::fromVector(std::move(myVector));

// Access read-only data
const uint8_t* ptr = blob.data();
size_t size = blob.size();
bool empty = blob.empty();
```

**Key properties:**
- Copying a frozen `Blob` is O(1) — it increments a shared reference count, sharing the underlying allocation.
- A blob must be frozen before passing it to `addImage` or `validateOnly`.
- `writableData()` asserts that the blob has not been frozen; calling it after `freeze()` is undefined behavior.

---

## Thread Safety

`Imager` methods are **safe to call from multiple threads concurrently**, with these notes:

- `addImage` / `addFile` serialize the dedup check and write through an internal mutex. This is by design: it prevents two threads from inserting the same file simultaneously.
- `getImage`, `listImages`, `getImagesByTags`, and other read methods run concurrently with each other and with ongoing writes.
- The `metrics()` reference is safe to read from any thread at any time.

Do not share a single `Imager` instance across processes. Each process must open its own instance. SQLite's WAL mode is used, which permits multiple concurrent readers from the same process.

---

## Usage Patterns

### Importing a Single File

```cpp
imager::AddResult r = lib.addFile("/path/to/photo.jpg");
switch (r.code) {
    case imager::ErrorCode::Ok:
        std::cout << "Added: " << r.id << "\n";
        break;
    case imager::ErrorCode::DuplicateFile:
        std::cout << "Already have this file: " << r.id << "\n";
        break;
    case imager::ErrorCode::BrokenFile:
        std::cerr << "File is corrupt: " << r.message << "\n";
        break;
    case imager::ErrorCode::UnsupportedFormat:
        std::cerr << "Format not supported\n";
        break;
    default:
        std::cerr << "Error: " << r.message << "\n";
}
```

### Tagging and Searching

```cpp
// Create tags (idempotent)
lib.createTag("beach");
lib.createTag("2024");

// Tag a file
lib.tagImage(photoId, "beach");
lib.tagImage(photoId, "2024");

// Search: get all 2024 beach photos, first page
auto photos = lib.getImagesByTags({"beach", "2024"}, 0, 50);

// Search: iterate all pages
uint32_t offset = 0;
const uint32_t pageSize = 100;
while (true) {
    auto page = lib.getImagesByTags({"beach"}, offset, pageSize);
    if (page.empty()) break;
    for (const auto& img : page) {
        // process img
    }
    offset += pageSize;
}
```

### Batch Import with Concurrency

`imagestore` handles batching and concurrency for you. If you are embedding the library and want to import concurrently from multiple threads, you can safely call `addFile` from multiple threads at the same time:

```cpp
std::vector<std::string> paths = { /* ... */ };
std::vector<std::future<imager::AddResult>> futures;

for (const auto& p : paths) {
    futures.push_back(std::async(std::launch::async, [&lib, p] {
        return lib.addFile(p);
    }));
}

for (auto& f : futures) {
    auto result = f.get();
    // handle result
}
```

The internal write mutex ensures dedup consistency. For high-throughput ingestion with thousands of files, prefer using `imagestore` which has a tuned concurrency model and progress reporting.
