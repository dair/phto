#pragma once

#include <string>

#include "ErrorCode.h"

namespace imager {

struct AddResult {
  ErrorCode code{ErrorCode::Ok};
  std::string id;      ///< Populated on success
  std::string message; ///< Error description on failure
};

} // namespace imager
