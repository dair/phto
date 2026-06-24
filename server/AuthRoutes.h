#pragma once

#include <crow.h>

#include <cstdint>

#include "AuthMiddleware.h"

namespace auth {

class TokenService;
class UserStore;

} // namespace auth

namespace server {

void registerAuthRoutes(
  crow::App<AuthMiddleware>& app, auth::UserStore& userStore, auth::TokenService& tokenService, uint32_t tokenTtlSeconds
);

} // namespace server
