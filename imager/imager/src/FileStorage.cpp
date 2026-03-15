#include "FileStorage.h"

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace imager {

FileStorage::FileStorage(std::vector<std::filesystem::path> roots)
    : m_roots(std::move(roots)) {}

std::filesystem::path FileStorage::filePath(const std::filesystem::path& root,
                                             const std::string& id,
                                             const std::string& ext) const {
    // ext has leading dot (e.g. ".jpg"); strip it for the filename component
    std::string extNoDot = (ext.size() > 1 && ext[0] == '.') ? ext.substr(1) : ext;
    return root / id.substr(0, 2) / (id + "." + extNoDot);
}

void FileStorage::writeFile(const std::string& id, const std::string& ext,
                             const uint8_t* data, size_t size) {
    std::vector<std::filesystem::path> written;
    written.reserve(m_roots.size());

    for (const auto& root : m_roots) {
        std::filesystem::path path = filePath(root, id, ext);
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                throw std::runtime_error("Cannot open for writing: " + path.string());
            }
            out.write(reinterpret_cast<const char*>(data),
                      static_cast<std::streamsize>(size));
            if (!out) {
                throw std::runtime_error("Write failed: " + path.string());
            }
            written.push_back(path);
        } catch (...) {
            // Roll back already-written copies
            for (const auto& p : written) {
                std::error_code ec;
                std::filesystem::remove(p, ec);
            }
            throw;
        }
    }
}

std::vector<uint8_t> FileStorage::readFile(const std::string& id,
                                            const std::string& ext) {
    for (const auto& root : m_roots) {
        std::filesystem::path path = filePath(root, id, ext);
        if (!std::filesystem::exists(path)) continue;

        std::ifstream in(path, std::ios::binary);
        if (!in) continue;

        std::vector<uint8_t> buf(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        return buf;
    }
    return {};
}

void FileStorage::deleteFile(const std::string& id, const std::string& ext) {
    for (const auto& root : m_roots) {
        std::error_code ec;
        std::filesystem::remove(filePath(root, id, ext), ec);
        // Ignore errors — best-effort deletion across all roots
    }
}

} // namespace imager
