#pragma once

#include <auth/TokenService.h>
#include <config/Config.h>
#include <crow.h>

#include <cstdint>
#include <optional>

#include "AuthMiddleware.h"
#include "ClientIp.h"

namespace auth {

class TokenService;
class UserStore;

} // namespace auth

namespace server {

/// Extract verified Claims from the request context, or nullopt (no/invalid token).
std::optional<auth::TokenService::Claims> requireAuth(crow::App<AuthMiddleware>& app, const crow::request& req);

/// Like requireAuth, but also enforces isAdmin. On failure writes a 401/403 into `res`
/// and returns nullopt — the handler must return `res` immediately.
std::optional<auth::TokenService::Claims> requireAdmin(
  crow::App<AuthMiddleware>& app, const crow::request& req, crow::response& res
);

/// Registers /auth/login, /auth/me, /auth/password. Brute-force thresholds and the lockout come from `authCfg`;
/// the throttle's client address is resolved through `proxies` (X-Forwarded-For only behind a trusted proxy).
/// `proxies` must outlive the app.
void registerAuthRoutes(
  crow::App<AuthMiddleware>& app,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  const config::AuthConfig& authCfg,
  const TrustedProxies& proxies
);

} // namespace server
