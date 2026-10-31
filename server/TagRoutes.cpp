#include "TagRoutes.h"

#include <imager/Imager.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "AuthRoutes.h"
#include "Json.h"

namespace server {

namespace {

constexpr size_t MAX_TAG_LEN = 255;

/// Serializes tag mutations so POST's read-modify-write cannot lose a concurrent update.
std::mutex g_tagMutex;

crow::response tagsResponse(const std::vector<std::string>& tags) {
  auto arr = crow::json::wvalue::list();
  for (const auto& t : tags) {
    arr.emplace_back(t);
  }
  crow::json::wvalue body;
  body["tags"] = std::move(arr);
  return jsonOk(200, body);
}

/// Non-empty, <= 255 bytes, no commas (GET /items?tags= splits on commas). Full charset rules: J1.
bool validTagName(const std::string& t) {
  return !t.empty() && t.size() <= MAX_TAG_LEN && t.find(',') == std::string::npos;
}

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

/// Percent-decodes a path segment ('+' stays literal). False on a malformed %XX escape.
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

/// Parses {"tags":[...]} (or, if `allowSingle`, {"tag":"x"}). Returns an error message, empty on success.
std::string parseTags(const crow::request& req, bool allowSingle, std::vector<std::string>& out) {
  auto body = crow::json::load(req.body);
  if (!body || body.t() != crow::json::type::Object) {
    return "body must be a JSON object";
  }
  if (body.has("tags")) {
    if (body["tags"].t() != crow::json::type::List) {
      return "tags must be an array of strings";
    }
    for (const auto& e : body["tags"]) {
      if (e.t() != crow::json::type::String) {
        return "tags must be an array of strings";
      }
      out.push_back(e.s());
    }
  } else if (allowSingle && body.has("tag")) {
    if (body["tag"].t() != crow::json::type::String) {
      return "tag must be a string";
    }
    out.push_back(body["tag"].s());
  } else {
    return allowSingle ? "body must contain tags or tag" : "body must contain tags";
  }
  for (const auto& t : out) {
    if (!validTagName(t)) {
      return "tag names must be 1-255 bytes and must not contain commas";
    }
  }
  return {};
}

/// Returns false (and sets `err`) if the item is absent or the lookup failed.
bool itemExists(imager::Imager& imager, const std::string& id, crow::response& err) {
  try {
    if (imager.getImage(id)) {
      return true;
    }
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: getImage threw: " << e.what();
    err = errorResponse(imager::ErrorCode::DatabaseError, "internal error");
    return false;
  }
  err = errorResponse(imager::ErrorCode::FileNotFound, "item not found");
  return false;
}

crow::response failure(imager::ErrorCode rc) {
  if (rc == imager::ErrorCode::FileNotFound) {
    return errorResponse(rc, "item not found");
  }
  return errorResponse(rc, "internal error");
}

crow::response setTags(imager::Imager& imager, const std::string& id, const std::vector<std::string>& tags) {
  try {
    const auto rc = imager.setImageTags(id, tags);
    if (rc != imager::ErrorCode::Ok) {
      return failure(rc);
    }
    return tagsResponse(imager.getImageTags(id));
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: set threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
}

crow::response addTags(imager::Imager& imager, const std::string& id, const std::vector<std::string>& add) {
  try {
    std::vector<std::string> merged = imager.getImageTags(id);
    for (const auto& t : add) {
      if (std::ranges::find(merged, t) == merged.end()) {
        merged.push_back(t);
      }
    }
    return setTags(imager, id, merged);
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: add threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
}

crow::response removeTag(imager::Imager& imager, const std::string& id, const std::string& tag) {
  try {
    const auto rc = imager.untagImage(id, tag);
    if (rc == imager::ErrorCode::Ok) {
      return crow::response{204};
    }
    // untagImage reports FileNotFound for both a missing item and an unbound tag.
    crow::response err;
    if (!itemExists(imager, id, err)) {
      return err;
    }
    if (rc == imager::ErrorCode::FileNotFound) {
      return errorResponse(rc, "tag not set on item");
    }
    return failure(rc);
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: remove threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
}

constexpr uint32_t DEFAULT_LIMIT = 50;
constexpr uint32_t MAX_LIMIT = 500;

crow::response listAllTags(imager::Imager& imager, const crow::request& req) {
  uint32_t offset = 0, limit = DEFAULT_LIMIT;
  if (!parseUintParam(req, "offset", 0, UINT32_MAX >> 1, offset)) {
    return badRequest("offset must be an integer in [0, 2147483647]");
  }
  if (!parseUintParam(req, "limit", 1, MAX_LIMIT, limit)) {
    return badRequest("limit must be an integer in [1, 500]");
  }
  try {
    return tagsResponse(imager.listTags(offset, limit));
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: list threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
}

crow::response createGlobalTag(imager::Imager& imager, const crow::request& req) {
  auto body = crow::json::load(req.body);
  if (!body || body.t() != crow::json::type::Object || !body.has("name") ||
      body["name"].t() != crow::json::type::String) {
    return badRequest("body must be a JSON object with a string name");
  }
  const std::string name = body["name"].s();
  if (!validTagName(name)) {
    return badRequest("tag names must be 1-255 bytes and must not contain commas");
  }
  try {
    std::lock_guard lock(g_tagMutex);
    const auto rc = imager.createTag(name);
    if (rc == imager::ErrorCode::DuplicateFile) {
      return errorResponse(rc, "tag already exists");
    }
    if (rc != imager::ErrorCode::Ok) {
      return failure(rc);
    }
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: create threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
  crow::json::wvalue out;
  out["name"] = name;
  return jsonOk(201, out);
}

crow::response deleteGlobalTag(imager::Imager& imager, const std::string& name) {
  try {
    std::lock_guard lock(g_tagMutex);
    const auto rc = imager.deleteTag(name);
    if (rc == imager::ErrorCode::Ok) {
      return crow::response{204};
    }
    return rc == imager::ErrorCode::FileNotFound ? errorResponse(rc, "tag not found") : failure(rc);
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "tags: delete threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
}

} // namespace

void registerTagRoutes(crow::App<AuthMiddleware>& app, imager::Imager& imager) {
  CROW_ROUTE(app, "/items/<string>/tags")
    .methods(crow::HTTPMethod::Get)([&app, &imager](const crow::request& req, const std::string& id) {
      if (!requireAuth(app, req)) {
        return unauthorized("authentication required");
      }
      if (!isLowerHex64(id)) {
        return badRequest("id must be 64 lowercase hex characters");
      }
      crow::response err;
      if (!itemExists(imager, id, err)) {
        return err;
      }
      return tagsResponse(imager.getImageTags(id));
    });

  CROW_ROUTE(app, "/items/<string>/tags")
    .methods(crow::HTTPMethod::Put, crow::HTTPMethod::Post)([&app,
                                                             &imager](const crow::request& req, const std::string& id) {
      if (!requireAuth(app, req)) {
        return unauthorized("authentication required");
      }
      if (!isLowerHex64(id)) {
        return badRequest("id must be 64 lowercase hex characters");
      }
      const bool isPut = req.method == crow::HTTPMethod::Put;
      std::vector<std::string> tags;
      if (auto msg = parseTags(req, !isPut, tags); !msg.empty()) {
        return badRequest(std::move(msg));
      }
      std::lock_guard lock(g_tagMutex);
      crow::response err;
      if (!itemExists(imager, id, err)) {
        return err;
      }
      return isPut ? setTags(imager, id, tags) : addTags(imager, id, tags);
    });

  CROW_ROUTE(app, "/items/<string>/tags/<string>")
    .methods(crow::HTTPMethod::Delete)(
      [&app, &imager](const crow::request& req, const std::string& id, const std::string& tag) {
        if (!requireAuth(app, req)) {
          return unauthorized("authentication required");
        }
        if (!isLowerHex64(id)) {
          return badRequest("id must be 64 lowercase hex characters");
        }
        std::string decoded;
        if (!percentDecode(tag, decoded)) {
          return badRequest("invalid percent-encoding in tag name");
        }
        if (!validTagName(decoded)) {
          return badRequest("invalid tag name");
        }
        std::lock_guard lock(g_tagMutex);
        return removeTag(imager, id, decoded);
      }
    );

  CROW_ROUTE(app, "/tags").methods(crow::HTTPMethod::Get)([&app, &imager](const crow::request& req) {
    if (!requireAuth(app, req)) {
      return unauthorized("authentication required");
    }
    return listAllTags(imager, req);
  });

  CROW_ROUTE(app, "/tags").methods(crow::HTTPMethod::Post)([&app, &imager](const crow::request& req) {
    if (!requireAuth(app, req)) {
      return unauthorized("authentication required");
    }
    return createGlobalTag(imager, req);
  });

  CROW_ROUTE(app, "/tags/<string>")
    .methods(crow::HTTPMethod::Delete)([&app, &imager](const crow::request& req, const std::string& name) {
      if (!requireAuth(app, req)) {
        return unauthorized("authentication required");
      }
      std::string decoded;
      if (!percentDecode(name, decoded)) {
        return badRequest("invalid percent-encoding in tag name");
      }
      if (!validTagName(decoded)) {
        return badRequest("invalid tag name");
      }
      return deleteGlobalTag(imager, decoded);
    });
}

} // namespace server
