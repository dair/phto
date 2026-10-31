#include "AccessLog.h"

#include <time.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <random>
#include <string>

#include "ServerMetrics.h"

namespace server {

namespace {

constexpr size_t MAX_JSON_FIELD = 2048;

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

std::string isoNow(bool millis) {
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  const time_t t = static_cast<time_t>(ms / 1000);
  struct tm tm{};
  ::gmtime_r(&t, &tm);
  char buf[40];
  size_t n = ::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
  if (millis) {
    n += static_cast<size_t>(::snprintf(buf + n, sizeof buf - n, ".%03d", static_cast<int>(ms % 1000)));
  }
  buf[n++] = 'Z';
  return std::string{buf, n};
}

/// Appends `s` as a quoted JSON string. Control characters, '"' and '\' are escaped; malformed UTF-8 (overlongs,
/// surrogates, > U+10FFFF, truncated sequences) is replaced by U+FFFD, so the output is always valid UTF-8 JSON.
void appendJsonString(std::string& out, std::string_view s) {
  static constexpr char HEX[] = "0123456789abcdef";
  out += '"';
  const size_t n = s.size() > MAX_JSON_FIELD ? MAX_JSON_FIELD : s.size();
  for (size_t i = 0; i < n;) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (c == '"' || c == '\\') {
        out += '\\';
        out += static_cast<char>(c);
      } else if (c < 0x20 || c == 0x7F) {
        out += "\\u00";
        out += HEX[c >> 4];
        out += HEX[c & 0xF];
      } else {
        out += static_cast<char>(c);
      }
      ++i;
      continue;
    }
    size_t len = 0;
    uint32_t cp = 0;
    uint32_t min = 0;
    if (c >= 0xC2 && c <= 0xDF) {
      len = 2, cp = c & 0x1F, min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
      len = 3, cp = c & 0x0F, min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4, cp = c & 0x07, min = 0x10000;
    }
    bool ok = len != 0 && i + len <= n;
    for (size_t k = 1; ok && k < len; ++k) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      ok = (cc & 0xC0) == 0x80;
      cp = (cp << 6) | (cc & 0x3F);
    }
    ok = ok && cp >= min && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
    if (ok) {
      out.append(s.data() + i, len);
      i += len;
    } else {
      out += "\xEF\xBF\xBD";
      ++i;
    }
  }
  if (s.size() > n) {
    out += "\xE2\x80\xA6"; // ellipsis marks truncation
  }
  out += '"';
}

void appendJsonStringOrNull(std::string& out, std::string_view s) {
  if (s.empty()) {
    out += "null";
  } else {
    appendJsonString(out, s);
  }
}

} // namespace

std::optional<AccessLog::Format> AccessLog::parseFormat(std::string_view s) noexcept {
  if (s == "text") {
    return Format::Text;
  }
  if (s == "json") {
    return Format::Json;
  }
  return std::nullopt;
}

std::string AccessLog::formatJson(
  std::string_view isoTimeMs,
  std::string_view clientIp,
  std::string_view login,
  std::string_view method,
  std::string_view path,
  int status,
  uint64_t responseBytes,
  uint64_t durationUs,
  std::string_view requestId
) {
  std::string line;
  line.reserve(256);
  line += "{\"ts\":";
  appendJsonString(line, isoTimeMs);
  line += ",\"client_ip\":";
  appendJsonString(line, clientIp);
  line += ",\"login\":";
  appendJsonStringOrNull(line, login);
  line += ",\"method\":";
  appendJsonString(line, method);
  line += ",\"route\":";
  appendJsonString(line, ServerMetrics::routeTemplate(ServerMetrics::classify(path)));
  line += ",\"path\":";
  appendJsonString(line, path);
  line += ",\"status\":";
  line += std::to_string(status);
  line += ",\"bytes\":";
  line += std::to_string(responseBytes);
  line += ",\"duration_ms\":";
  line += std::to_string(durationUs / 1000);
  line += '.';
  char frac[8];
  ::snprintf(frac, sizeof frac, "%03u", static_cast<unsigned>(durationUs % 1000));
  line += frac;
  line += ",\"request_id\":";
  appendJsonString(line, requestId);
  line += "}\n";
  return line;
}

bool validRequestId(std::string_view id) noexcept {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  for (const char ch : id) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
                    ch == '_' || ch == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

std::string generateRequestId() {
  thread_local std::mt19937_64 rng{std::random_device{}()};
  static constexpr char HEX[] = "0123456789abcdef";
  std::string id(32, '0');
  for (size_t w = 0; w < 2; ++w) {
    uint64_t v = rng();
    for (size_t k = 0; k < 16; ++k) {
      id[w * 16 + k] = HEX[v & 0xF];
      v >>= 4;
    }
  }
  return id;
}

const std::string& ensureRequestId(crow::request& req) {
  static const std::string NAME = "X-Request-Id";
  const auto it = req.headers.find(NAME);
  if (it != req.headers.end() && req.headers.count(NAME) == 1 && validRequestId(it->second)) {
    return it->second;
  }
  req.headers.erase(NAME);
  return req.headers.emplace(NAME, generateRequestId())->second;
}

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
  const crow::request& req, int status, uint64_t responseBytes, std::string_view login, uint64_t durationUs
) {
  if (!m_enabled) {
    return;
  }
  const std::string clientIp = m_proxies.clientIp(req.remote_ip_address, req.get_header_value("X-Forwarded-For"));
  const std::string line = m_format == Format::Json ? formatJson(
                                                        isoNow(true),
                                                        clientIp,
                                                        login,
                                                        crow::method_name(req.method),
                                                        req.url,
                                                        status,
                                                        responseBytes,
                                                        durationUs,
                                                        req.get_header_value("X-Request-Id")
                                                      )
                                                    : format(
                                                        isoNow(false),
                                                        clientIp,
                                                        login,
                                                        crow::method_name(req.method),
                                                        req.url,
                                                        status,
                                                        responseBytes,
                                                        durationUs / 1000
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
