#pragma once

#include <cstddef>

enum ValidationResult {
  INVALID = -1, // the input is MOV/MP4 data that is actually broken
  VALID = 0,    // the input is valid MOV/MP4 data with at least one video stream
  WRONG = 1,    // the input is not MOV/MP4 data at all
};

ValidationResult validateMov(const void* data, size_t dataSize);
