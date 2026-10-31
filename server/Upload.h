#pragma once

#include <crow.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "AccessLog.h"
#include "AuthMiddleware.h"

namespace server {

/// Spool file `<tmpdir>/phto-upload-XXXXXX`, filled in bounded chunks and unlinked on destruction / discard().
/// Every live spool is registered so removeAllSpools() can clean up when the process is forced down.
class Spool {
public:
  /// Creates the file. Check ok() before use. `limit` bounds the total bytes accepted by write().
  explicit Spool(uint64_t limit = UINT64_MAX);
  ~Spool();

  Spool(const Spool&) = delete;
  Spool& operator=(const Spool&) = delete;

  bool ok() const noexcept {
    return m_fd >= 0;
  }

  enum class Result {
    Ok,
    TooLarge,
    IoError,
  };

  /// Appends data (buffered in a fixed 64 KiB block).
  Result write(const char* data, size_t n);

  /// Flushes and closes the descriptor; the file stays on disk for reading. False on I/O error.
  bool finish();

  /// Closes and unlinks now (idempotent).
  void discard() noexcept;

  uint64_t size() const noexcept {
    return m_total;
  }

  const std::filesystem::path& path() const noexcept {
    return m_path;
  }

private:
  bool flush();

  std::filesystem::path m_path;
  int m_fd{-1};
  uint64_t m_limit;
  uint64_t m_total{0};
  std::string m_buf;
};

/// Unlinks every spool file still alive (called before a forced exit, where destructors will not run).
void removeAllSpools() noexcept;

/// Installs the request-size policy on the app (Crow header filter + body sink):
///  * Content-Length above the route's limit -> 413 before the body is read, connection closed;
///  * chunked / unknown-length bodies are cut off with 413 as soon as they pass the limit;
///  * authenticated `PUT /items` bodies stream into a Spool (limit `maxUploadBytes`), everything else is buffered
///    in request::body with the 64 KiB JSON limit. Rejected requests bypass the middleware, so they are access-logged
///    (and given the standard hardening headers) here.
void installRequestPolicy(crow::App<AuthMiddleware>& app, uint64_t maxUploadBytes, AccessLog& accessLog);

/// The spool filled for this request by the policy, or nullptr (not an authenticated PUT /items).
Spool* uploadSpool(const crow::request& req);

} // namespace server
