#pragma once

#include <auth/TokenService.h>
#include <crow.h>

#include <optional>
#include <string>

namespace server {

/// Crow global middleware that extracts and verifies the Bearer token from
/// the Authorization header. Stores the result in context::claims.
/// Never rejects requests — enforcement is per-handler via requireAuth().
struct AuthMiddleware {
  auth::TokenService* service{nullptr};

  struct context {
    std::optional<auth::TokenService::Claims> claims;
  };

  void before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
    ctx.claims = std::nullopt;
    if (service == nullptr) {
      return;
    }
    const std::string& auth = req.get_header_value("Authorization");
    static constexpr std::string_view PREFIX = "Bearer ";
    if (auth.size() > PREFIX.size() && auth.substr(0, PREFIX.size()) == PREFIX) {
      ctx.claims = service->verify(std::string_view{auth}.substr(PREFIX.size()));
    }
  }

  void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {}
};

} // namespace server
