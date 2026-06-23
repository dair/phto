#pragma once

#include <config/Config.h>
#include <imager/Types.h>
#include <metrics/Metrics.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace imager {

class Imager {
public:
  /// Construct from a parsed configuration.
  explicit Imager(const config::AppConfig& cfg);
  ~Imager();

  Imager(const Imager&) = delete;
  Imager& operator=(const Imager&) = delete;
  Imager(Imager&&) = delete;
  Imager& operator=(Imager&&) = delete;

  // ---- Core operations ----

  /// Add an image/video from a Blob.
  /// Validates format, computes SHA256, checks for duplicates,
  /// writes to all storage roots, then inserts into all DBs.
  AddResult addImage(const Blob& blob, const std::string& filename);

  /// Read a file from disk and process it through the full addImage pipeline.
  /// The file read happens inside the library, making it observable via metrics.
  ///
  /// @param path     Absolute or relative path to the file on disk.
  /// @param filename Display name for the file (used for extension detection
  ///                 and stored in the database). If empty, the path's
  ///                 filename component is used.
  /// @return AddResult with the outcome.
  AddResult addFile(
    const std::filesystem::path& path, const std::string& filename = "", StageCallback onStage = nullptr
  );

  /// Validate format and compute SHA256 without writing to storage or DB.
  /// Performs extension check, validation, hashing, and duplicate detection.
  /// Returns Ok (with hash id) if valid and not a duplicate, DuplicateFile if
  /// already stored, or an error code if the file is broken/unsupported.
  AddResult validateOnly(const Blob& blob, const std::string& filename);

  /// Like validateOnly but for files that may be too large to load into a Blob.
  /// Uses streaming validation for files above the streaming threshold.
  AddResult validateOnlyFile(const std::filesystem::path& path, const std::string& filename = "");

  /// Get image metadata + tags by ID. Returns nullopt if not found.
  std::optional<ImageInfo> getImage(const std::string& id);

  /// Get images matching ALL given tags (AND semantics), paginated.
  std::vector<ImageInfo> getImagesByTags(
    const std::vector<std::string>& tags, uint32_t offset = 0, uint32_t limit = 50
  );

  /// Get images that have no tags, paginated.  Tags field in each ImageInfo is empty.
  std::vector<ImageInfo> getUntaggedImages(uint32_t offset = 0, uint32_t limit = 50);

  // ---- Additional operations ----

  /// Delete an image by ID (removes from DB and all storage roots).
  ErrorCode deleteImage(const std::string& id);

  /// Compute SHA256 of `blob` bytes (no validation, no extension check) and delete
  /// whatever record matches from all databases, all storage roots, and all sidecars.
  /// Returns Ok + computed id on success, FileNotFound + id if nothing matched.
  DeleteResult deleteBlob(const Blob& blob);

  /// Read `path` from disk, compute its SHA256 (streaming if over the size threshold),
  /// and delete the matching record. No validation, no extension check.
  /// The optional stage callback receives Reading -> Hashing -> WaitingMutex ->
  /// DedupChecking (repurposed as "looking up id").
  /// Returns Ok + id on success, FileNotFound + id if nothing matched.
  DeleteResult deleteFile(const std::filesystem::path& path, StageCallback onStage = nullptr);

  /// Compute SHA256 of `blob` without mutating any state. Useful for dry-run checks.
  /// Returns the hex SHA256 string, or empty string on error.
  std::string hashOnlyBlob(const Blob& blob);

  /// Read `path` and compute its SHA256 without mutating any state.
  /// Respects the streaming threshold (256 MB). Useful for dry-run checks.
  /// Returns the hex SHA256 string, or empty string on error.
  std::string hashOnlyFile(const std::filesystem::path& path, StageCallback onStage = nullptr);

  /// Add a tag to an image.
  ErrorCode tagImage(const std::string& id, const std::string& tag);

  /// Remove a tag from an image.
  ErrorCode untagImage(const std::string& id, const std::string& tag);

  /// Get all tags for an image.
  std::vector<std::string> getImageTags(const std::string& id);

  /// Get raw image data (reads from first available storage root).
  Blob getImageData(const std::string& id);

  /// Create a new tag.
  ErrorCode createTag(const std::string& name);

  /// Delete a tag (unbinds from all images).
  ErrorCode deleteTag(const std::string& name);

  /// List all tags, paginated.
  std::vector<std::string> listTags(uint32_t offset = 0, uint32_t limit = 50);

  /// List all images, paginated.
  std::vector<ImageInfo> listImages(uint32_t offset = 0, uint32_t limit = 50);

  /// Get total image count.
  uint64_t imageCount();

  /// Access the metrics instance owned by this Imager.
  const metrics::Metrics& metrics() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace imager
