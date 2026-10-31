#pragma once

#include <crow/json.h>
#include <imager/types/ErrorCode.h>

#include <cstdint>
#include <string>
#include <string_view>

// Forward-declare crow types to keep this header light.
namespace crow {

struct request;
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

/// True if `s` is exactly 64 lowercase hex characters (an item id).
bool isLowerHex64(std::string_view s);

/// Percent-decodes a path segment ('+' stays literal). False on a malformed %XX escape.
bool percentDecode(const std::string& in, std::string& out);

/// JSON response with the given status and Content-Type: application/json.
crow::response jsonOk(int status, const crow::json::wvalue& body);

/// Parses an optional unsigned query parameter in [lo, hi]; `out` is untouched if absent.
/// False on non-numeric/out-of-range.
bool parseUintParam(const crow::request& req, const char* name, uint32_t lo, uint32_t hi, uint32_t& out);

} // namespace server
