#pragma once

#include <imager/ImageValidator.h>

namespace imager {

std::unique_ptr<validation::IStreamValidator> createPngStreamValidator();

} // namespace imager
