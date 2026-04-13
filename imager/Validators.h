#pragma once

#include <imager/ImageValidator.h>

#include <memory>
#include <vector>

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

/// Factory: returns one IStreamValidator per format.
/// Implemented in Jpeg/Png/Heic/Mov/Nef/AaeStreamValidatorImpl.cpp.
std::unique_ptr<validation::IStreamValidator> createJpegStreamValidator();
std::unique_ptr<validation::IStreamValidator> createPngStreamValidator();
std::unique_ptr<validation::IStreamValidator> createHeicStreamValidator();
std::unique_ptr<validation::IStreamValidator> createMovStreamValidator();
std::unique_ptr<validation::IStreamValidator> createNefStreamValidator();
std::unique_ptr<validation::IStreamValidator> createAaeStreamValidator();

std::vector<std::unique_ptr<validation::IStreamValidator>> createDefaultStreamValidators();

} // namespace imager
