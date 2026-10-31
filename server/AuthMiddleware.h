#pragma once

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <crow.h>

#include <optional>
#include <string>
#include <string_view>

#include "Json.h"

namespace server {

/// Crow global middleware that extracts and verifies the Bearer token from
/// the Authorization header. Stores the result in context::claims.
/// A valid token is additionally re-checked against the user store (one indexed point query):
/// deleted or disabled users get no claims, and isAdmin/fullName come from the DB, so
/// demotion takes effect immediately. Never rejects on auth grounds — enforcement is per-handler via requireAuth().
///
/// Also the single place for cross-cutting hardening (§13): oversized non-upload bodies are rejected with 413
/// before any handler parses them, and every response (including 401/403/404/405 and error envelopes) gets
/// `X-Content-Type-Options: nosniff` and, unless a handler set one, `Cache-Control: no-store`.
struct AuthMiddleware {
  auth::TokenService* service{nullptr};
  auth::UserStore* users{nullptr};

  struct context {
    std::optional<auth::TokenService::Claims> claims;
  };

  void before_handle(crow::request& req, crow::response& res, context& ctx) {
    ctx.claims = std::nullopt;
    // Crow has no global request-size limit (the body is already buffered here), so cap before any parsing.
    // PUT /items is bounded separately by max_upload_mb.
    if (req.body.size() > JSON_BODY_LIMIT && !(req.method == crow::HTTPMethod::Put && req.url == "/items")) {
      res = tooLarge("request body exceeds 64 KiB");
      res.end();
      return;
    }
    if (service == nullptr) {
      return;
    }
    const std::string& auth = req.get_header_value("Authorization");
    static constexpr std::string_view PREFIX = "Bearer ";
    if (auth.size() > PREFIX.size() && auth.substr(0, PREFIX.size()) == PREFIX) {
      ctx.claims = service->verify(std::string_view{auth}.substr(PREFIX.size()));
      if (ctx.claims && users != nullptr) {
        const auto user = users->get(ctx.claims->login);
        if (!user || !user->enabled) {
          ctx.claims = std::nullopt;
        } else {
          ctx.claims->isAdmin = user->isAdmin;
          ctx.claims->fullName = user->fullName;
        }
      }
    }
  }

  void after_handle(crow::request& /*req*/, crow::response& res, context& /*ctx*/) {
    res.set_header("X-Content-Type-Options", "nosniff");
    if (res.get_header_value("Cache-Control").empty()) {
      res.set_header("Cache-Control", "no-store");
    }
  }
};

} // namespace server
