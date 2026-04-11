#pragma once

namespace imager {

enum class ErrorCode {
  Ok,
  BrokenFile,        ///< Validation failed
  DuplicateFile,     ///< SHA256+size already exists
  UnsupportedFormat, ///< Extension not recognized
  FileNotFound,      ///< ID does not exist in the database
  StorageError,      ///< Filesystem I/O failure
  AmbiguousSidecar,  ///< Sidecar file matches multiple potential parent files
  DatabaseError,     ///< Underlying DB error
  ConfigError,       ///< Configuration problem
};

} // namespace imager
