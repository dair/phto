#pragma once

#include <crow.h>

#include <cstdint>

#include "AuthMiddleware.h"

namespace auth {

class UserStore;

} // namespace auth

namespace server {

/// Registers the admin-only /users routes (GET/POST /users, GET/PATCH/DELETE /users/{login}).
/// Passwords are hashed with `pbkdf2Iterations`.
void registerUserRoutes(crow::App<AuthMiddleware>& app, auth::UserStore& userStore, uint32_t pbkdf2Iterations);

} // namespace server
