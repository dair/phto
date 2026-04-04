#pragma once

#include <memory>
#include <vector>

#include "imager/ImageValidator.h"

namespace imager {

/// Factory: returns one IValidator per image/video format.
/// Implemented in JpegValidatorImpl.cpp, PngValidatorImpl.cpp, HeicValidatorImpl.cpp,
/// NefValidatorImpl.cpp, and MovValidatorImpl.cpp.
std::unique_ptr<validation::IValidator> createJpegValidator();
std::unique_ptr<validation::IValidator> createPngValidator();
std::unique_ptr<validation::IValidator> createHeicValidator();
std::unique_ptr<validation::IValidator> createNefValidator();
std::unique_ptr<validation::IValidator> createMovValidator();

inline std::vector<std::unique_ptr<validation::IValidator>> createDefaultValidators() {
  std::vector<std::unique_ptr<validation::IValidator>> v;
  v.push_back(createJpegValidator());
  v.push_back(createPngValidator());
  v.push_back(createHeicValidator());
  v.push_back(createNefValidator());
  v.push_back(createMovValidator());
  return v;
}

} // namespace imager
