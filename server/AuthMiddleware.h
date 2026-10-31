#pragma once

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <crow.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "AccessLog.h"
#include "Json.h"
#include "ServerMetrics.h"

namespace server {

/// Crow global middleware that extracts and verifies the Bearer token from
/// the Authorization header. Stores the result in context::claims.
/// A valid token is additionally re-checked against the user store (one indexed point query):
/// deleted or disabled users get no claims, and isAdmin/fullName come from the DB, so
/// demotion takes effect immediately. Never rejects on auth grounds — enforcement is per-handler via requireAuth().
///
/// Also the single place for cross-cutting hardening (§13): every response (including 401/403/404/405 and error
/// envelopes) gets `X-Content-Type-Options: nosniff` and, unless a handler set one, `Cache-Control: no-store`, and
/// writes the access-log line. Request-size limits are enforced earlier, while the request is being read
/// (server/Upload: Crow header filter + body sink); those rejections never reach the middleware, so that code adds
/// the same headers and log line itself.
struct AuthMiddleware {
  auth::TokenService* service{nullptr};
  auth::UserStore* users{nullptr};
  AccessLog* accessLog{nullptr};
  ServerMetrics* metrics{nullptr};

  struct context {
    std::optional<auth::TokenService::Claims> claims;
    std::chrono::steady_clock::time_point start{}; // epoch = before_handle did not run for this request
  };

  /// Verified claims for the request's Bearer token (re-checked against the user store), or nullopt.
  std::optional<auth::TokenService::Claims> authenticate(const crow::request& req) const {
    if (service == nullptr) {
      return std::nullopt;
    }
    const std::string& auth = req.get_header_value("Authorization");
    static constexpr std::string_view PREFIX = "Bearer ";
    if (auth.size() <= PREFIX.size() || auth.substr(0, PREFIX.size()) != PREFIX) {
      return std::nullopt;
    }
    auto claims = service->verify(std::string_view{auth}.substr(PREFIX.size()));
    if (claims && users != nullptr) {
      const auto user = users->get(claims->login);
      if (!user || !user->enabled) {
        return std::nullopt;
      }
      claims->isAdmin = user->isAdmin;
      claims->fullName = user->fullName;
    }
    return claims;
  }

  void before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
    ctx.start = std::chrono::steady_clock::now();
    ensureRequestId(req);
    if (metrics != nullptr) {
      metrics->beginRequest();
    }
    ctx.claims = authenticate(req);
  }

  void after_handle(crow::request& req, crow::response& res, context& ctx) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Request-Id", ensureRequestId(req));
    if (res.get_header_value("Cache-Control").empty()) {
      res.set_header("Cache-Control", "no-store");
    }
    const bool started = ctx.start != std::chrono::steady_clock::time_point{};
    const auto elapsed = started ? std::chrono::steady_clock::now() - ctx.start : std::chrono::steady_clock::duration{};
    if (metrics != nullptr) {
      if (started) {
        metrics->endRequest();
      }
      metrics->record(
        req.method,
        req.url,
        res.code,
        started ? std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() : ServerMetrics::NO_LATENCY
      );
    }
    if (accessLog != nullptr && accessLog->enabled()) {
      const auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
      uint64_t bytes = res.body.size();
      if (bytes == 0 && res.is_static_type()) { // streamed file: the body is empty, Crow sets Content-Length
        const std::string& cl = res.get_header_value("Content-Length");
        std::from_chars(cl.data(), cl.data() + cl.size(), bytes);
      }
      accessLog->log(
        req, res.code, bytes, ctx.claims ? std::string_view{ctx.claims->login} : "", static_cast<uint64_t>(us)
      );
    }
    // A later request on this connection that skips before_handle (e.g. no route) must not inherit either.
    ctx.start = {};
    ctx.claims = std::nullopt;
  }
};

} // namespace server
