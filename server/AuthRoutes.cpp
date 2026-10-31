#include "AuthRoutes.h"

#include <auth/PasswordHash.h>
#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <auth/Validation.h>
#include <auth/types/User.h>
#include <crow.h>
#include <database/Database.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

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

/// Throttles shared by the login and password-change routes; one pair per registerAuthRoutes() call.
struct Throttles {
  LoginThrottle login; ///< per login name
  LoginThrottle ip;    ///< per client IP (higher threshold): rotating logins cannot dodge it
  const TrustedProxies& proxies;
  const int lockoutSeconds;

  explicit Throttles(const config::AuthConfig& cfg, const TrustedProxies& tp)
    : login(static_cast<int>(cfg.maxLoginFailures), static_cast<int>(cfg.lockoutSeconds)),
      ip(static_cast<int>(cfg.maxIpFailures), static_cast<int>(cfg.lockoutSeconds)),
      proxies(tp),
      lockoutSeconds(static_cast<int>(cfg.lockoutSeconds)) {}

  /// Throttle key for the caller: the forwarded client behind a trusted proxy, else the peer address.
  std::string clientKey(const crow::request& req) const {
    return proxies.clientIp(req.remote_ip_address, req.get_header_value("X-Forwarded-For"));
  }

  crow::response tooMany(const char* what) const {
    auto res = jsonError(429, "TooManyRequests", what);
    res.add_header("Retry-After", std::to_string(lockoutSeconds));
    return res;
  }
};

constexpr size_t MAX_LOGIN_BYTES = auth::MAX_LOGIN_LEN;
constexpr size_t MAX_PASSWORD_BYTES = auth::MAX_PASSWORD_LEN;

} // namespace

void registerAuthRoutes(
  crow::App<AuthMiddleware>& app,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  const config::AuthConfig& authCfg,
  const TrustedProxies& proxies
) {
  const uint32_t tokenTtlSeconds = authCfg.tokenTtlSeconds;
  const uint32_t pbkdf2Iterations = authCfg.pbkdf2Iterations;
  auto throttles = std::make_shared<Throttles>(authCfg, proxies);

  // POST /auth/login — issue a JWT for valid credentials (brute-force throttled)
  CROW_ROUTE(app, "/auth/login")
    .methods(crow::HTTPMethod::Post)([&userStore, &tokenService, tokenTtlSeconds, throttles](const crow::request& req) {
      auto body = crow::json::load(req.body);
      if (!body || body.t() != crow::json::type::Object || !body.has("login") || !body.has("password") ||
          body["login"].t() != crow::json::type::String || body["password"].t() != crow::json::type::String) {
        return badRequest("body must contain string login and password");
      }

      const std::string login = body["login"].s();
      const std::string password = body["password"].s();
      if (login.size() > MAX_LOGIN_BYTES || password.size() > MAX_PASSWORD_BYTES) {
        return badRequest("login or password too long");
      }

      // Throttle check — precedes any credential verification.
      const std::string clientKey = throttles->clientKey(req);
      if (throttles->ip.isLockedOut(clientKey) || throttles->login.isLockedOut(login)) {
        return throttles->tooMany("too many failed login attempts — try again later");
      }

      auto userOpt = userStore.get(login);
      auto pwOpt = userStore.getPassword(login);

      // Generic failure for missing user or wrong password — no user enumeration.
      if (!userOpt || !pwOpt || !auth::verifyPassword(password, *pwOpt)) {
        throttles->login.recordFailure(login);
        throttles->ip.recordFailure(clientKey);
        return unauthorized("invalid credentials");
      }

      const auth::User& user = *userOpt;
      if (!user.enabled) {
        // Don't penalise the throttle counter for disabled-account attempts.
        return forbidden("account disabled");
      }

      throttles->login.recordSuccess(login);

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
    .methods(crow::HTTPMethod::Post)([&app, &userStore, pbkdf2Iterations, throttles](const crow::request& req) {
      auto claims = requireAuth(app, req);
      if (!claims) {
        return unauthorized("authentication required");
      }

      auto body = crow::json::load(req.body);
      if (!body || body.t() != crow::json::type::Object || !body.has("oldPassword") || !body.has("newPassword") ||
          body["oldPassword"].t() != crow::json::type::String || body["newPassword"].t() != crow::json::type::String) {
        return badRequest("body must contain string oldPassword and newPassword");
      }

      const std::string oldPassword = body["oldPassword"].s();
      const std::string newPassword = body["newPassword"].s();

      if (auth::validatePassword(newPassword) != nullptr) {
        return badRequest("newPassword must be 8..1024 bytes");
      }
      if (oldPassword.size() > MAX_PASSWORD_BYTES) {
        return badRequest("oldPassword too long");
      }

      // Brute-force guard: shares the per-login and per-IP counters with POST /auth/login, so guessing the
      // current password through either endpoint draws from one budget. Checked before any PBKDF2 work.
      const std::string& login = claims->login;
      const std::string clientKey = throttles->clientKey(req);
      if (throttles->ip.isLockedOut(clientKey) || throttles->login.isLockedOut(login)) {
        return throttles->tooMany("too many failed password attempts — try again later");
      }

      auto pwOpt = userStore.getPassword(login);
      if (!pwOpt || !auth::verifyPassword(oldPassword, *pwOpt)) {
        throttles->login.recordFailure(login);
        throttles->ip.recordFailure(clientKey);
        return unauthorized("current password is incorrect");
      }
      throttles->login.recordSuccess(login);

      const auth::PasswordRecord newRec = auth::hashPassword(newPassword, pbkdf2Iterations);
      userStore.setPassword(claims->login, newRec);

      return crow::response{204};
    });
}

} // namespace server
