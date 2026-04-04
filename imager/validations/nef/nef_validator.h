#pragma once

#include <cstddef>

enum ValidationResult {
  INVALID = -1, // the input is NEF data that is actually broken
  VALID = 0,    // the input is valid NEF data
  WRONG = 1,    // the input is not NEF data at all
};

ValidationResult validateNef(const void* data, size_t dataSize);
