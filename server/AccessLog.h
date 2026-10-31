#pragma once

#include <crow.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "ClientIp.h"

namespace server {

/// One line per request on stderr, written after the response is produced:
///   <YYYY-MM-DDTHH:MM:SSZ> <client-ip> <login|-> <METHOD> <path> <status> <response-bytes|-> <duration-ms>ms
/// The path never includes the query string; bytes outside printable ASCII (and spaces) are written as \xHH, so a
/// line always has exactly eight space-separated fields. Headers (Authorization, cookies) and bodies are never logged.
class AccessLog {
public:
  AccessLog(bool enabled, const TrustedProxies& proxies)
    : m_enabled(enabled),
      m_proxies(proxies) {}

  bool enabled() const noexcept {
    return m_enabled;
  }

  /// Formats and writes one line (a single write(2) under a mutex, so lines never interleave).
  void log(const crow::request& req, int status, uint64_t responseBytes, std::string_view login, uint64_t durationMs);

  /// Builds the line without writing it (exposed for tests).
  static std::string format(
    std::string_view isoTime,
    std::string_view clientIp,
    std::string_view login,
    std::string_view method,
    std::string_view path,
    int status,
    uint64_t responseBytes,
    uint64_t durationMs
  );

private:
  const bool m_enabled;
  const TrustedProxies& m_proxies;
  std::mutex m_mutex;
};

} // namespace server
