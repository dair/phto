#pragma once

#include <memory>
#include <vector>

#include <imager/ImageValidator.h>

namespace imager {

/// Factory: returns one IValidator per image/video/sidecar format.
/// Implemented in JpegValidatorImpl.cpp, PngValidatorImpl.cpp, HeicValidatorImpl.cpp,
/// NefValidatorImpl.cpp, MovValidatorImpl.cpp, and AaeValidatorImpl.cpp.
std::unique_ptr<validation::IValidator> createJpegValidator();
std::unique_ptr<validation::IValidator> createPngValidator();
std::unique_ptr<validation::IValidator> createHeicValidator();
std::unique_ptr<validation::IValidator> createNefValidator();
std::unique_ptr<validation::IValidator> createMovValidator();
std::unique_ptr<validation::IValidator> createAaeValidator();

std::vector<std::unique_ptr<validation::IValidator>> createDefaultValidators();

} // namespace imager
