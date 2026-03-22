#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imager {

struct ImageInfo {
    std::string              id;      ///< SHA256 hex string (64 chars)
    std::string              name;    ///< Original filename
    uint64_t                 size{0}; ///< File size in bytes
    std::string              ext;     ///< Lowercase extension with dot (e.g. ".jpg")
    std::vector<std::string> tags;
};

} // namespace imager
