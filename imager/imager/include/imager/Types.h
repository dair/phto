#pragma once

#include "types/Blob.h"

#include <cstdint>
#include <string>
#include <vector>

namespace imager {

enum class ErrorCode {
    Ok,
    BrokenFile,       ///< Validation failed
    DuplicateFile,    ///< SHA256+size already exists
    UnsupportedFormat,///< Extension not recognized
    FileNotFound,     ///< ID does not exist in the database
    StorageError,     ///< Filesystem I/O failure
    DatabaseError,    ///< Underlying DB error
    ConfigError       ///< Configuration problem
};

struct ImageInfo {
    std::string              id;   ///< SHA256 hex string (64 chars)
    std::string              name; ///< Original filename
    uint64_t                 size{0}; ///< File size in bytes
    std::string              ext;  ///< Lowercase extension with dot (e.g. ".jpg")
    std::vector<std::string> tags;
};

struct AddResult {
    ErrorCode   code{ErrorCode::Ok};
    std::string id;      ///< Populated on success
    std::string message; ///< Error description on failure
};

} // namespace imager
