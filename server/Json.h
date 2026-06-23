#pragma once

#include <imager/types/ErrorCode.h>

#include <string>

// Forward-declare crow types to keep this header light.
namespace crow {

struct response;

} // namespace crow

namespace server {

/// Returns the HTTP status code for an imager::ErrorCode.
int httpStatusFor(imager::ErrorCode code);

/// Returns the stringified enum name for an imager::ErrorCode (e.g. "BrokenFile").
std::string errorCodeName(imager::ErrorCode code);

/// Builds a uniform JSON error envelope: {"error":{"code":"...","message":"..."}}
/// with Content-Type: application/json and the given status.
crow::response jsonError(int status, std::string code, std::string message);

/// Convenience: derives status and code string from an imager::ErrorCode.
crow::response errorResponse(imager::ErrorCode code, std::string message);

// --- Transport-level helpers (HTTP-level codes, not ErrorCode values) ---

/// 400 Bad Request — code "BadRequest".
crow::response badRequest(std::string message);

/// 401 Unauthorized — code "Unauthorized"; sets WWW-Authenticate: Bearer.
crow::response unauthorized(std::string message);

/// 403 Forbidden — code "Forbidden".
crow::response forbidden(std::string message);

} // namespace server
