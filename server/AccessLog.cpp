#include "AccessLog.h"

#include <time.h>
#include <unistd.h>

#include <chrono>
#include <string>

namespace server {

namespace {

constexpr size_t MAX_FIELD = 512; // truncate absurd paths; keeps a line bounded

/// Appends `s` with every byte outside 0x21..0x7E escaped as \xHH. Empty -> "-".
void appendEscaped(std::string& out, std::string_view s) {
  if (s.empty()) {
    out += '-';
    return;
  }
  static constexpr char HEX[] = "0123456789abcdef";
  const size_t n = s.size() > MAX_FIELD ? MAX_FIELD : s.size();
  for (size_t i = 0; i < n; ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c > 0x20 && c < 0x7F && c != '\\') {
      out += static_cast<char>(c);
    } else {
      out += "\\x";
      out += HEX[c >> 4];
      out += HEX[c & 0xF];
    }
  }
  if (s.size() > n) {
    out += "...";
  }
}

std::string isoNow() {
  const time_t t = ::time(nullptr);
  struct tm tm{};
  ::gmtime_r(&t, &tm);
  char buf[32];
  const size_t n = ::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string{buf, n};
}

} // namespace

std::string AccessLog::format(
  std::string_view isoTime,
  std::string_view clientIp,
  std::string_view login,
  std::string_view method,
  std::string_view path,
  int status,
  uint64_t responseBytes,
  uint64_t durationMs
) {
  std::string line;
  line.reserve(128);
  line += isoTime;
  line += ' ';
  appendEscaped(line, clientIp);
  line += ' ';
  appendEscaped(line, login);
  line += ' ';
  appendEscaped(line, method);
  line += ' ';
  appendEscaped(line, path);
  line += ' ';
  line += std::to_string(status);
  line += ' ';
  if (responseBytes == 0) {
    line += '-';
  } else {
    line += std::to_string(responseBytes);
  }
  line += ' ';
  line += std::to_string(durationMs);
  line += "ms\n";
  return line;
}

void AccessLog::log(
  const crow::request& req, int status, uint64_t responseBytes, std::string_view login, uint64_t durationMs
) {
  if (!m_enabled) {
    return;
  }
  const std::string line = format(
    isoNow(),
    m_proxies.clientIp(req.remote_ip_address, req.get_header_value("X-Forwarded-For")),
    login,
    crow::method_name(req.method),
    req.url,
    status,
    responseBytes,
    durationMs
  );
  std::lock_guard lock{m_mutex};
  const char* p = line.data();
  size_t left = line.size();
  while (left > 0) {
    const ssize_t w = ::write(STDERR_FILENO, p, left);
    if (w <= 0) {
      break;
    }
    p += w;
    left -= static_cast<size_t>(w);
  }
}

} // namespace server
