#pragma once

#include <cstddef>

enum ValidationResult {
  INVALID = -1, // the input is AAE data that is actually broken/malformed XML
  VALID = 0,    // the input is a valid AAE plist file
  WRONG = 1,    // the input is not an AAE file at all
};

ValidationResult validateAae(const void* data, size_t dataSize);
