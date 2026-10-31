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
  {imager::ErrorCode::DuplicateFile, 409, "Conflict"},
  {imager::ErrorCode::UnsupportedFormat, 415, "UnsupportedFormat"},
  {imager::ErrorCode::FileNotFound, 404, "NotFound"},
  {imager::ErrorCode::StorageError, 500, "Internal"},
  {imager::ErrorCode::AmbiguousSidecar, 409, "Conflict"},
  {imager::ErrorCode::DatabaseError, 500, "Internal"},
  {imager::ErrorCode::ConfigError, 500, "Internal"},
  {imager::ErrorCode::TooLarge, 413, "TooLarge"},
};

int hexVal(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

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
  return m ? std::string{m->name} : "Internal";
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

crow::response notFound(std::string message) {
  return jsonError(404, "NotFound", std::move(message));
}

crow::response conflict(std::string message) {
  return jsonError(409, "Conflict", std::move(message));
}

crow::response tooLarge(std::string message) {
  return jsonError(413, "TooLarge", std::move(message));
}

crow::response internalError() {
  return jsonError(500, "Internal", "internal error");
}

bool isValidUtf8(std::string_view s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  const size_t n = s.size();
  for (size_t i = 0; i < n;) {
    const unsigned char c = p[i];
    size_t len;
    uint32_t cp;
    if (c < 0x80) {
      ++i;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF) {
      len = 2;
      cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
      len = 3;
      cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + len > n) {
      return false;
    }
    for (size_t k = 1; k < len; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) {
        return false;
      }
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      return false;
    }
    i += len;
  }
  return true;
}

bool validTagName(std::string_view s) {
  return !s.empty() && s.size() <= 255 && isValidUtf8(s) && std::ranges::none_of(s, [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F || u == ',';
  });
}

bool isLowerHex64(std::string_view s) {
  return s.size() == 64 &&
         std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool percentDecode(const std::string& in, std::string& out) {
  out.clear();
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '%') {
      out.push_back(in[i]);
      continue;
    }
    const int hi = i + 2 < in.size() ? hexVal(in[i + 1]) : -1;
    const int lo = i + 2 < in.size() ? hexVal(in[i + 2]) : -1;
    if (hi < 0 || lo < 0) {
      return false;
    }
    out.push_back(static_cast<char>(hi * 16 + lo));
    i += 2;
  }
  return true;
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

bool parsePage(const crow::request& req, uint32_t& offset, uint32_t& limit, crow::response& err) {
  if (!parseUintParam(req, "offset", 0, UINT32_MAX >> 1, offset)) {
    err = badRequest("offset must be an integer in [0, 2147483647]");
    return false;
  }
  if (!parseUintParam(req, "limit", 1, MAX_PAGE_LIMIT, limit)) {
    err = badRequest("limit must be an integer in [1, 500]");
    return false;
  }
  return true;
}

} // namespace server
