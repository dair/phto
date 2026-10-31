#pragma once

#include <imager/ValidationResult.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace validation {

/// Abstract base for format validators (buffer-based).
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
