#include "Json.h"

#include <crow.h>

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
  {imager::ErrorCode::BrokenFile, 422, "BrokenFile"},
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

} // namespace server
