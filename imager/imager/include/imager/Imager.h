#pragma once

#include "imager/Types.h"
#include "config/Config.h"

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

    Imager(const Imager&)            = delete;
    Imager& operator=(const Imager&) = delete;

    // ---- Core operations ----

    /// Add an image/video from a Blob.
    /// Validates format, computes SHA256, checks for duplicates,
    /// writes to all storage roots, then inserts into all DBs.
    AddResult addImage(const Blob& blob, const std::string& filename);

    /// Get image metadata + tags by ID. Returns nullopt if not found.
    std::optional<ImageInfo> getImage(const std::string& id);

    /// Get images matching ALL given tags (AND semantics), paginated.
    std::vector<ImageInfo> getImagesByTags(
        const std::vector<std::string>& tags,
        uint32_t offset = 0,
        uint32_t limit  = 50);

    // ---- Additional operations ----

    /// Delete an image by ID (removes from DB and all storage roots).
    ErrorCode deleteImage(const std::string& id);

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace imager
