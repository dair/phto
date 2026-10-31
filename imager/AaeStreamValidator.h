#pragma once

#include <imager/IStreamValidator.h>

#include <memory>

namespace imager {

std::unique_ptr<validation::IStreamValidator> createAaeStreamValidator();

} // namespace imager
