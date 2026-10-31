#include "AuthRoutes.h"

#include <auth/PasswordHash.h>
#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <auth/types/User.h>
#include <crow.h>
#include <database/Database.h>

#include <algorithm>
#include <chrono>
#include <cstdint>

#include "Json.h"
#include "LoginThrottle.h"

namespace server {

std::optional<auth::TokenService::Claims> requireAuth(crow::App<AuthMiddleware>& app, const crow::request& req) {
  return app.get_context<AuthMiddleware>(req).claims;
}

/// Like requireAuth, but also enforces isAdmin. Returns the Claims if the caller
/// is an authenticated admin. On failure, writes an appropriate error response
/// (401 or 403) and returns nullopt — the handler must return immediately.
std::optional<auth::TokenService::Claims> requireAdmin(
  crow::App<AuthMiddleware>& app, const crow::request& req, crow::response& res
) {
  auto claims = requireAuth(app, req);
  if (!claims) {
    res = unauthorized("authentication required");
    return std::nullopt;
  }
  if (!claims->isAdmin) {
    res = forbidden("admin privileges required");
    return std::nullopt;
  }
  return claims;
}

namespace {

/// Shared throttle instance — one per process, lives for the duration of the daemon.
LoginThrottle& throttle() {
  static LoginThrottle instance;
  return instance;
}

} // namespace

void registerAuthRoutes(
  crow::App<AuthMiddleware>& app,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  uint32_t tokenTtlSeconds,
  uint32_t pbkdf2Iterations
) {
  // POST /auth/login — issue a JWT for valid credentials (brute-force throttled)
  CROW_ROUTE(app, "/auth/login")
    .methods(crow::HTTPMethod::Post)([&userStore, &tokenService, tokenTtlSeconds](const crow::request& req) {
      auto body = crow::json::load(req.body);
      if (!body || !body.has("login") || !body.has("password")) {
        return badRequest("body must contain login and password");
      }

      const std::string login = body["login"].s();
      const std::string password = body["password"].s();

      // Throttle check — precedes any credential verification.
      if (throttle().isLockedOut(login)) {
        auto res = jsonError(429, "TooManyRequests", "too many failed login attempts — try again later");
        res.add_header("Retry-After", std::to_string(LoginThrottle::LOCKOUT_SECONDS));
        return res;
      }

      auto userOpt = userStore.get(login);
      auto pwOpt = userStore.getPassword(login);

      // Generic failure for missing user or wrong password — no user enumeration.
      if (!userOpt || !pwOpt || !auth::verifyPassword(password, *pwOpt)) {
        throttle().recordFailure(login);
        return unauthorized("invalid credentials");
      }

      const auth::User& user = *userOpt;
      if (!user.enabled) {
        // Don't penalise the throttle counter for disabled-account attempts.
        return forbidden("account disabled");
      }

      throttle().recordSuccess(login);

      const std::string token = tokenService.issue(user);
      const uint64_t expiresAt =
        static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()
        ) +
        tokenTtlSeconds;

      crow::json::wvalue resp;
      resp["token"] = token;
      resp["expiresAt"] = expiresAt;
      resp["login"] = user.login;
      resp["fullName"] = user.fullName;
      resp["isAdmin"] = user.isAdmin;

      crow::response res{200, resp};
      res.add_header("Content-Type", "application/json");
      return res;
    });

  // GET /auth/me — return claims from the verified Bearer token
  CROW_ROUTE(app, "/auth/me").methods(crow::HTTPMethod::Get)([&app](const crow::request& req) {
    auto claims = requireAuth(app, req);
    if (!claims) {
      return unauthorized("authentication required");
    }

    crow::json::wvalue resp;
    resp["login"] = claims->login;
    resp["fullName"] = claims->fullName;
    resp["isAdmin"] = claims->isAdmin;

    crow::response res{200, resp};
    res.add_header("Content-Type", "application/json");
    return res;
  });

  // POST /auth/password — self-service password change (authenticated users only)
  CROW_ROUTE(app, "/auth/password")
    .methods(crow::HTTPMethod::Post)([&app, &userStore, pbkdf2Iterations](const crow::request& req) {
      auto claims = requireAuth(app, req);
      if (!claims) {
        return unauthorized("authentication required");
      }

      auto body = crow::json::load(req.body);
      if (!body || !body.has("oldPassword") || !body.has("newPassword")) {
        return badRequest("body must contain oldPassword and newPassword");
      }

      const std::string oldPassword = body["oldPassword"].s();
      const std::string newPassword = body["newPassword"].s();

      if (newPassword.size() < 8) {
        return badRequest("newPassword must be at least 8 characters");
      }

      auto pwOpt = userStore.getPassword(claims->login);
      if (!pwOpt || !auth::verifyPassword(oldPassword, *pwOpt)) {
        return unauthorized("current password is incorrect");
      }

      const auth::PasswordRecord newRec = auth::hashPassword(newPassword, pbkdf2Iterations);
      userStore.setPassword(claims->login, newRec);

      return crow::response{204};
    });
}

} // namespace server
