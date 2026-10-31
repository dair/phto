#pragma once

#include <string>

namespace validation {

/// Result of a validation attempt.
struct ValidationResult {
  bool valid{false};
  std::string errorMessage; ///< Empty if valid
};

} // namespace validation
