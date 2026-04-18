#pragma once

#include <string>

#include "ErrorCode.h"

namespace imager {

struct DeleteResult {
  ErrorCode code{ErrorCode::Ok};
  std::string id;      ///< The SHA256 we computed (populated even when FileNotFound)
  std::string message; ///< Error detail on failure
};

} // namespace imager
