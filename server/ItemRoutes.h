#pragma once

#include <crow.h>

#include <cstdint>

#include "AuthMiddleware.h"

namespace imager {

class Imager;

} // namespace imager

namespace server {

/// Registers PUT /items, GET/DELETE /items/{id}, GET /items/{id}/content. All require a Bearer token.
void registerItemRoutes(crow::App<AuthMiddleware>& app, imager::Imager& imager, uint64_t maxUploadBytes);

} // namespace server
