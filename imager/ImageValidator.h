#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace validation {

/// Result of a validation attempt.
struct ValidationResult {
  bool valid{false};
  std::string errorMessage; ///< Empty if valid
};

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

/// Stream-based validator for large files.
/// The validator opens and reads the file itself via its library's native
/// file I/O APIs. No full-file allocation is ever required.
class IStreamValidator {
public:
  virtual ~IStreamValidator() = default;

  /// Returns true if this validator handles the given extension.
  /// Extension is lowercase with leading dot (e.g. ".mov").
  virtual bool supportsExtension(const std::string& ext) const = 0;

  /// Validate a file by path. The implementation opens the file itself
  /// and reads as needed — it must not hold the entire file in memory.
  virtual ValidationResult validateFile(const std::filesystem::path& path) const = 0;
};

} // namespace validation
