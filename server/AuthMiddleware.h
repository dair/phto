#pragma once

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <crow.h>

#include <optional>
#include <string>

namespace server {

/// Crow global middleware that extracts and verifies the Bearer token from
/// the Authorization header. Stores the result in context::claims.
/// A valid token is additionally re-checked against the user store (one indexed point query):
/// deleted or disabled users get no claims, and isAdmin/fullName come from the DB, so
/// demotion takes effect immediately. Never rejects requests — enforcement is per-handler via requireAuth().
struct AuthMiddleware {
  auth::TokenService* service{nullptr};
  auth::UserStore* users{nullptr};

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

  void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {}
};

} // namespace server
