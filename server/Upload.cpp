#include "Upload.h"

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "Json.h"

namespace server {

namespace {

namespace fs = std::filesystem;

constexpr size_t SPOOL_BLOCK = 64 * 1024;

std::mutex& registryMutex() {
  static std::mutex m;
  return m;
}

std::unordered_set<std::string>& registry() {
  static std::unordered_set<std::string> r;
  return r;
}

/// Spool stored in request::user_data.
struct UploadState {
  Spool spool;

  explicit UploadState(uint64_t limit)
    : spool(limit) {}
};

/// Rejection response with the hardening headers the middleware would have added.
crow::response hardened(crow::response r) {
  r.set_header("X-Content-Type-Options", "nosniff");
  r.set_header("Cache-Control", "no-store");
  return r;
}

/// Fills `res`, logs the request and returns false (the "reject" return value of the Crow hooks).
bool reject(AccessLog& log, const crow::request& req, crow::response& res, crow::response r, std::string_view login) {
  res = hardened(std::move(r));
  log.log(req, res.code, res.body.size(), login, 0);
  return false;
}

std::optional<uint64_t> declaredLength(const crow::request& req) {
  const std::string& v = req.get_header_value("Content-Length");
  uint64_t n = 0;
  if (v.empty() || std::from_chars(v.data(), v.data() + v.size(), n).ec != std::errc{}) {
    return std::nullopt;
  }
  return n;
}

} // namespace

Spool::Spool(uint64_t limit)
  : m_limit(limit) {
  std::error_code ec;
  std::string tpl = (fs::temp_directory_path(ec) / "phto-upload-XXXXXX").string();
  if (ec) {
    return;
  }
  m_fd = ::mkstemp(tpl.data());
  if (m_fd < 0) {
    return;
  }
  m_path = std::move(tpl);
  m_buf.reserve(SPOOL_BLOCK);
  std::lock_guard lock{registryMutex()};
  registry().insert(m_path.string());
}

Spool::~Spool() {
  discard();
}

bool Spool::flush() {
  const char* p = m_buf.data();
  size_t left = m_buf.size();
  while (left > 0) {
    const ssize_t w = ::write(m_fd, p, left);
    if (w <= 0) {
      return false;
    }
    p += w;
    left -= static_cast<size_t>(w);
  }
  m_buf.clear();
  return true;
}

Spool::Result Spool::write(const char* data, size_t n) {
  if (m_fd < 0) {
    return Result::IoError;
  }
  if (n > m_limit - std::min(m_total, m_limit)) {
    return Result::TooLarge;
  }
  m_total += n;
  while (n > 0) {
    const size_t take = std::min(n, SPOOL_BLOCK - m_buf.size());
    m_buf.append(data, take);
    data += take;
    n -= take;
    if (m_buf.size() == SPOOL_BLOCK && !flush()) {
      return Result::IoError;
    }
  }
  return Result::Ok;
}

bool Spool::finish() {
  if (m_fd < 0) {
    return false;
  }
  const bool ok = flush();
  const bool closed = ::close(m_fd) == 0;
  m_fd = -1;
  return ok && closed;
}

void Spool::discard() noexcept {
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  if (!m_path.empty()) {
    ::unlink(m_path.c_str());
    {
      std::lock_guard lock{registryMutex()};
      registry().erase(m_path.string());
    }
    m_path.clear();
  }
}

void removeAllSpools() noexcept {
  std::lock_guard lock{registryMutex()};
  for (const std::string& p : registry()) {
    ::unlink(p.c_str());
  }
  registry().clear();
}

Spool* uploadSpool(const crow::request& req) {
  auto* st = static_cast<UploadState*>(req.user_data.get());
  return st != nullptr ? &st->spool : nullptr;
}

void installRequestPolicy(crow::App<AuthMiddleware>& app, uint64_t maxUploadBytes, AccessLog& accessLog) {
  AuthMiddleware& mw = app.get_middleware<AuthMiddleware>();
  const uint64_t uploadLimit = maxUploadBytes != 0 ? maxUploadBytes : UINT64_MAX;

  app.header_filter([&mw, &accessLog, uploadLimit](crow::request& req, crow::response& res) {
    const bool uploadRoute = req.method == crow::HTTPMethod::Put && req.url == "/items";
    std::string login;
    bool spooled = false;
    if (uploadRoute) {
      // Only an authenticated caller may stream a large body to disk; anyone else gets the small JSON cap
      // (and the handler's own 401).
      if (const auto claims = mw.authenticate(req)) {
        spooled = true;
        login = claims->login;
      }
    }
    const uint64_t limit = spooled ? uploadLimit : JSON_BODY_LIMIT;
    const char* what = spooled ? "upload exceeds max_upload_mb" : "request body exceeds 64 KiB";

    const auto declared = declaredLength(req);
    if (declared && *declared > limit) {
      if (uploadRoute && !spooled) {
        return reject(accessLog, req, res, unauthorized("authentication required"), login);
      }
      return reject(accessLog, req, res, tooLarge(what), login);
    }

    if (!(declared && *declared > 0) && req.get_header_value("Transfer-Encoding").empty()) {
      return true; // no body: nothing to bound or spool
    }

    if (!spooled) {
      std::string* body = &req.body; // the request outlives its sink
      req.body_sink = [body, &accessLog, &req, login, what](const char* p, size_t n, crow::response& res) {
        if (body->size() + n > JSON_BODY_LIMIT) {
          return reject(accessLog, req, res, tooLarge(what), login);
        }
        body->append(p, n);
        return true;
      };
      return true;
    }

    auto state = std::make_shared<UploadState>(limit);
    if (!state->spool.ok()) {
      CROW_LOG_ERROR << "PUT /items: cannot create spool file";
      return reject(
        accessLog, req, res, errorResponse(imager::ErrorCode::StorageError, "internal storage error"), login
      );
    }
    Spool* spool = &state->spool;
    req.user_data = std::move(state);
    req.body_sink = [spool, &accessLog, &req, login, what](const char* p, size_t n, crow::response& res) {
      switch (spool->write(p, n)) {
        case Spool::Result::Ok:
          return true;
        case Spool::Result::TooLarge:
          return reject(accessLog, req, res, tooLarge(what), login);
        default:
          CROW_LOG_ERROR << "PUT /items: spool write failed";
          return reject(
            accessLog, req, res, errorResponse(imager::ErrorCode::StorageError, "internal storage error"), login
          );
      }
    };
    return true;
  });
}

} // namespace server
