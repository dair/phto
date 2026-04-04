#pragma once

#include <cstddef>

enum ValidationResult {
  INVALID = -1, // the input is HEIC/HEIF data that is actually broken
  VALID = 0,    // the input is valid HEIC/HEIF data
  WRONG = 1,    // the input is not HEIC/HEIF data at all
};

ValidationResult validateHeic(const void* data, size_t dataSize);
