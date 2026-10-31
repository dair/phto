#pragma once

#include <stdexcept>
#include <string>

namespace db {

enum class DatabaseErrorCode {
  CreationFailed,      ///< New database file could not be created
  OpenFailed,          ///< Existing database file could not be opened
  QueryFailed,         ///< A SQL statement failed to execute
  NotFound,            ///< Requested entity does not exist
  ConstraintViolation, ///< Unique/FK constraint was violated
};

class DatabaseException: public std::runtime_error {
public:
  DatabaseException(DatabaseErrorCode code, const std::string& message);
  DatabaseErrorCode code() const noexcept;

private:
  DatabaseErrorCode m_code;
};

} // namespace db
