#pragma once

#include <crow.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "ClientIp.h"

namespace server {

/// One line per request on stderr, written after the response is produced:
///   <YYYY-MM-DDTHH:MM:SSZ> <client-ip> <login|-> <METHOD> <path> <status> <response-bytes|-> <duration-ms>ms
/// The path never includes the query string; bytes outside printable ASCII (and spaces) are written as \xHH, so a
/// line always has exactly eight space-separated fields. Headers (Authorization, cookies) and bodies are never logged.
///
/// With `[server].access_log_format = "json"` each line is instead one compact JSON object (see formatJson); the text
/// format never contains the request id.
class AccessLog {
public:
  enum class Format {
    Text,
    Json,
  };

  /// Parses "text" / "json"; nullopt for anything else.
  static std::optional<Format> parseFormat(std::string_view s) noexcept;

  AccessLog(bool enabled, const TrustedProxies& proxies, Format format = Format::Text)
    : m_enabled(enabled),
      m_proxies(proxies),
      m_format(format) {}

  bool enabled() const noexcept {
    return m_enabled;
  }

  /// Formats and writes one line (a single write(2) under a mutex, so lines never interleave).
  void log(const crow::request& req, int status, uint64_t responseBytes, std::string_view login, uint64_t durationUs);

  /// JSON line (with trailing '\n'), fields in this order: ts (RFC 3339 UTC, ms), client_ip, login (null if empty),
  /// method, route (ServerMetrics template or "other"), path, status, bytes (always a number, 0 for empty), duration_ms
  /// (number, microsecond resolution), request_id. Strings are escaped per RFC 8259; invalid UTF-8 becomes U+FFFD.
  static std::string formatJson(
    std::string_view isoTimeMs,
    std::string_view clientIp,
    std::string_view login,
    std::string_view method,
    std::string_view path,
    int status,
    uint64_t responseBytes,
    uint64_t durationUs,
    std::string_view requestId
  );

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
  const Format m_format;
  std::mutex m_mutex;
};

/// True for ^[A-Za-z0-9._-]{1,64}$.
bool validRequestId(std::string_view id) noexcept;

/// 32 lowercase hex chars from a per-thread PRNG (seeded once from std::random_device; never blocks afterwards).
std::string generateRequestId();

/// Idempotent: keeps a valid inbound X-Request-Id, otherwise replaces it with a generated one, and returns it.
/// Called from the header filter and the middleware, so every request carries exactly one valid id.
const std::string& ensureRequestId(crow::request& req);

} // namespace server
