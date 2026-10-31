#pragma once

#include <crow.h>

#include "AuthMiddleware.h"

namespace imager {

class Imager;

} // namespace imager

namespace server {

/// Registers GET/PUT/POST /items/{id}/tags and DELETE /items/{id}/tags/{tag}. All require a Bearer token.
void registerTagRoutes(crow::App<AuthMiddleware>& app, imager::Imager& imager);

} // namespace server
