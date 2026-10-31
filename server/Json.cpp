#include "Json.h"

#include <crow.h>

#include <algorithm>
#include <charconv>

namespace server {

namespace {

struct StatusMapping {
  imager::ErrorCode code;
  int httpStatus;
  const char* name;
};

// Single-sourced ErrorCode → (HTTP status, string) table (§7 of 0022.SERVER.md).
constexpr StatusMapping MAPPINGS[] = {
  {imager::ErrorCode::Ok, 200, "Ok"},
  {imager::ErrorCode::BrokenFile, 400, "BrokenFile"},
  {imager::ErrorCode::DuplicateFile, 409, "DuplicateFile"},
  {imager::ErrorCode::UnsupportedFormat, 415, "UnsupportedFormat"},
  {imager::ErrorCode::FileNotFound, 404, "FileNotFound"},
  {imager::ErrorCode::StorageError, 500, "StorageError"},
  {imager::ErrorCode::AmbiguousSidecar, 409, "AmbiguousSidecar"},
  {imager::ErrorCode::DatabaseError, 500, "DatabaseError"},
  {imager::ErrorCode::ConfigError, 500, "ConfigError"},
  {imager::ErrorCode::TooLarge, 413, "TooLarge"},
};

const StatusMapping* findMapping(imager::ErrorCode code) {
  for (const auto& m : MAPPINGS) {
    if (m.code == code) {
      return &m;
    }
  }
  return nullptr;
}

} // namespace

int httpStatusFor(imager::ErrorCode code) {
  const auto* m = findMapping(code);
  return m ? m->httpStatus : 500;
}

std::string errorCodeName(imager::ErrorCode code) {
  const auto* m = findMapping(code);
  return m ? std::string{m->name} : "UnknownError";
}

crow::response jsonError(int status, std::string code, std::string message) {
  crow::json::wvalue body;
  body["error"]["code"] = std::move(code);
  body["error"]["message"] = std::move(message);
  crow::response res{status, body};
  res.add_header("Content-Type", "application/json");
  return res;
}

crow::response errorResponse(imager::ErrorCode code, std::string message) {
  return jsonError(httpStatusFor(code), errorCodeName(code), std::move(message));
}

crow::response badRequest(std::string message) {
  return jsonError(400, "BadRequest", std::move(message));
}

crow::response unauthorized(std::string message) {
  auto res = jsonError(401, "Unauthorized", std::move(message));
  res.add_header("WWW-Authenticate", "Bearer");
  return res;
}

crow::response forbidden(std::string message) {
  return jsonError(403, "Forbidden", std::move(message));
}

bool isLowerHex64(std::string_view s) {
  return s.size() == 64 &&
         std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

crow::response jsonOk(int status, const crow::json::wvalue& body) {
  crow::response res{status, body.dump()};
  res.add_header("Content-Type", "application/json");
  return res;
}

/// Parses an optional unsigned query parameter in [lo, hi]; false on non-numeric/out-of-range.
bool parseUintParam(const crow::request& req, const char* name, uint32_t lo, uint32_t hi, uint32_t& out) {
  const char* v = req.url_params.get(name);
  if (!v) {
    return true;
  }
  std::string_view sv{v};
  uint64_t n = 0;
  auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), n);
  if (sv.empty() || ec != std::errc{} || p != sv.data() + sv.size() || n < lo || n > hi) {
    return false;
  }
  out = static_cast<uint32_t>(n);
  return true;
}

} // namespace server
