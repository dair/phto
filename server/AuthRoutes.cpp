#include "AuthRoutes.h"

#include <auth/PasswordHash.h>
#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <crow.h>

#include <chrono>
#include <cstdint>

#include "Json.h"

namespace server {

namespace {

/// Extract and return verified Claims from the request context, or nullopt.
std::optional<auth::TokenService::Claims> requireAuth(crow::App<AuthMiddleware>& app, const crow::request& req) {
  return app.get_context<AuthMiddleware>(req).claims;
}

} // namespace

void registerAuthRoutes(
  crow::App<AuthMiddleware>& app, auth::UserStore& userStore, auth::TokenService& tokenService, uint32_t tokenTtlSeconds
) {
  // POST /auth/login — issue a JWT for valid credentials
  CROW_ROUTE(app, "/auth/login")
    .methods(crow::HTTPMethod::Post)([&userStore, &tokenService, tokenTtlSeconds](const crow::request& req) {
      auto body = crow::json::load(req.body);
      if (!body || !body.has("login") || !body.has("password")) {
        return badRequest("body must contain login and password");
      }

      const std::string login = body["login"].s();
      const std::string password = body["password"].s();

      auto userOpt = userStore.get(login);
      auto pwOpt = userStore.getPassword(login);

      // Generic failure for missing user or wrong password — no user enumeration.
      if (!userOpt || !pwOpt || !auth::verifyPassword(password, *pwOpt)) {
        return unauthorized("invalid credentials");
      }

      const auth::User& user = *userOpt;
      if (!user.enabled) {
        return forbidden("account disabled");
      }

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
}

} // namespace server
