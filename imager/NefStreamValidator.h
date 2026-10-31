#pragma once

#include <imager/IStreamValidator.h>

#include <memory>

namespace imager {

std::unique_ptr<validation::IStreamValidator> createNefStreamValidator();

} // namespace imager
