#pragma once

#include <cstddef>

enum ValidationResult {
    INVALID = -1, // the input is JPEG data that is actually broken
    VALID   =  0, // the input is valid JPEG data
    WRONG   =  1, // the input is not JPEG data at all
};

ValidationResult validateJpeg(const void* data, size_t dataSize);
