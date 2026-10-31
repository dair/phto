#pragma once

#include <crow.h>

#include <cstdint>

#include "AuthMiddleware.h"

namespace imager {

class Imager;

} // namespace imager

namespace server {

/// Registers PUT /items, GET/DELETE /items/{id}, GET /items/{id}/content. All require a Bearer token.
/// Upload size limits are enforced while the request is read (server/Upload: installRequestPolicy).
void registerItemRoutes(crow::App<AuthMiddleware>& app, imager::Imager& imager);

} // namespace server
