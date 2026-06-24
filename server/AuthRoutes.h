#pragma once

#include <auth/TokenService.h>
#include <crow.h>

#include <cstdint>
#include <optional>

#include "AuthMiddleware.h"

namespace auth {

class TokenService;
class UserStore;

} // namespace auth

namespace server {

/// Extract verified Claims from the request context, or nullopt (no/invalid token).
std::optional<auth::TokenService::Claims> requireAuth(crow::App<AuthMiddleware>& app, const crow::request& req);

void registerAuthRoutes(
  crow::App<AuthMiddleware>& app,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  uint32_t tokenTtlSeconds,
  uint32_t pbkdf2Iterations
);

void registerUserRoutes(crow::App<AuthMiddleware>& app, auth::UserStore& userStore);

} // namespace server
