#include "DatabaseException.h"

namespace db {

DatabaseException::DatabaseException(DatabaseErrorCode code, const std::string& message)
  : std::runtime_error(message),
    m_code(code) {}

DatabaseErrorCode DatabaseException::code() const noexcept {
  return m_code;
}

} // namespace db
