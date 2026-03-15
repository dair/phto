#pragma once

#include <cstdint>
#include <string>

namespace validation {

/// Result of a validation attempt.
struct ValidationResult {
    bool        valid{false};
    std::string errorMessage; ///< Empty if valid
};

/// Abstract base for format validators.
class IValidator {
public:
    virtual ~IValidator() = default;

    /// Returns true if this validator handles the given extension.
    /// Extension is lowercase with leading dot (e.g. ".jpg").
    virtual bool supportsExtension(const std::string& ext) const = 0;

    /// Validate raw file data. Returns success or an error description.
    virtual ValidationResult validate(const uint8_t* data, size_t size) const = 0;
};

} // namespace validation
