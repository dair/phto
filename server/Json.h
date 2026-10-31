#pragma once

#include <crow/json.h>
#include <imager/types/ErrorCode.h>

#include <cstddef>
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

/// Returns the API envelope `code` for an imager::ErrorCode. The vocabulary is generic (not facade-specific):
/// Ok, BadRequest-class codes are separate helpers; mapping: DuplicateFile/AmbiguousSidecar -> "Conflict",
/// FileNotFound -> "NotFound", Storage/Database/ConfigError -> "Internal"; others keep their enum name.
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

/// 404 Not Found — code "NotFound".
crow::response notFound(std::string message);

/// 409 Conflict — code "Conflict".
crow::response conflict(std::string message);

/// 413 Payload Too Large — code "TooLarge".
crow::response tooLarge(std::string message);

/// 500 — code "Internal"; the message is always generic (details belong in the log).
crow::response internalError();

/// Maximum accepted request body for every route except PUT /items (which is bounded by max_upload_mb).
inline constexpr size_t JSON_BODY_LIMIT = 64 * 1024;

/// Listing bounds shared by every paginated route.
inline constexpr uint32_t DEFAULT_PAGE_LIMIT = 50;
inline constexpr uint32_t MAX_PAGE_LIMIT = 500;

/// Parses ?offset (0..2^31-1) and ?limit (1..MAX_PAGE_LIMIT); absent values leave the defaults.
/// On failure sets `err` to a 400 and returns false.
bool parsePage(const crow::request& req, uint32_t& offset, uint32_t& limit, crow::response& err);

/// True if `s` is well-formed UTF-8 (no overlongs, surrogates or code points above U+10FFFF).
bool isValidUtf8(std::string_view s);

/// Tag names: 1..255 bytes, valid UTF-8, no comma (the list filter splits on it), no control characters
/// (bytes < 0x20 and 0x7F, including NUL).
bool validTagName(std::string_view s);

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
