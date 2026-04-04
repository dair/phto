#pragma once

#include <memory>
#include <vector>

#include "imager/ImageValidator.h"

namespace imager {

/// Factory: returns one IValidator per image format (JPEG, PNG).
/// Implemented in JpegValidatorImpl.cpp and PngValidatorImpl.cpp.
std::unique_ptr<validation::IValidator> createJpegValidator();
std::unique_ptr<validation::IValidator> createPngValidator();

inline std::vector<std::unique_ptr<validation::IValidator>> createDefaultValidators() {
  std::vector<std::unique_ptr<validation::IValidator>> v;
  v.push_back(createJpegValidator());
  v.push_back(createPngValidator());
  return v;
}

} // namespace imager
