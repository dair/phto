#pragma once

#include <cstddef>

enum ValidationResult {
    INVALID = -1, // the input is PNG data that is actually broken
    VALID   =  0, // the input is valid PNG data
    WRONG   =  1, // the input is not PNG data at all
};

ValidationResult validatePng(const void* data, size_t dataSize);
