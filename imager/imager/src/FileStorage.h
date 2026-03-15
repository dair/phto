#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace imager {

/// Manages file I/O across multiple redundant storage roots.
/// File layout within each root: <root>/<first-2-hex-chars>/<sha256>.<ext-no-dot>
class FileStorage {
public:
    explicit FileStorage(std::vector<std::filesystem::path> roots);

    /// Write data to ALL roots synchronously.
    /// On any write failure, previously written copies are cleaned up and
    /// an exception is thrown.
    void writeFile(const std::string& id,
                   const std::string& ext, ///< Lowercase, with leading dot
                   const uint8_t*     data,
                   size_t             size);

    /// Read from the first available root. Returns empty vector if not found.
    std::vector<uint8_t> readFile(const std::string& id, const std::string& ext);

    /// Delete from all roots (errors silently ignored — best effort).
    void deleteFile(const std::string& id, const std::string& ext);

private:
    std::vector<std::filesystem::path> m_roots;

    std::filesystem::path filePath(const std::filesystem::path& root,
                                   const std::string& id,
                                   const std::string& ext) const;
};

} // namespace imager
