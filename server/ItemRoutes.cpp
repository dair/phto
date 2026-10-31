#include "ItemRoutes.h"

#include <imager/Imager.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "AuthRoutes.h"
#include "Json.h"
#include "Upload.h"

namespace server {

namespace {

namespace fs = std::filesystem;

constexpr size_t ID_LEN = 64;

/// Strip any directory components (both separators) from a client-supplied name.
std::string baseName(std::string name) {
  auto pos = name.find_last_of("/\\");
  return pos == std::string::npos ? name : name.substr(pos + 1);
}

crow::json::wvalue imageToJson(const imager::ImageInfo& info) {
  crow::json::wvalue body;
  body["id"] = info.id;
  body["name"] = info.name;
  body["size"] = info.size;
  body["ext"] = info.ext;
  auto tags = crow::json::wvalue::list();
  for (const auto& t : info.tags) {
    tags.emplace_back(t);
  }
  body["tags"] = std::move(tags);
  return body;
}

/// GET /items: ?tags=a,b (AND; `tags` may repeat) | ?untagged=true | none; ?offset&limit.
crow::response listItems(imager::Imager& imager, const crow::request& req) {
  uint32_t offset = 0, limit = DEFAULT_PAGE_LIMIT;
  crow::response err;
  if (!parsePage(req, offset, limit, err)) {
    return err;
  }

  // Crow decodes query values in place into C strings, so an encoded NUL silently truncates a value.
  // Reject it from the raw query instead.
  if (const auto q = req.raw_url.find('?'); q != std::string::npos && req.raw_url.find("%00", q) != std::string::npos) {
    return badRequest("query must not contain NUL bytes");
  }

  bool untagged = false;
  if (const char* u = req.url_params.get("untagged")) {
    std::string_view sv{u};
    if (sv != "true" && sv != "false") {
      return badRequest("untagged must be true or false");
    }
    untagged = sv == "true";
  }

  std::vector<std::string> tags;
  const auto tagParams = req.url_params.get_list("tags", false);
  for (const std::string& raw : tagParams) {
    std::string_view rest = raw;
    while (true) {
      auto comma = rest.find(',');
      std::string_view t = rest.substr(0, comma);
      if (t.empty()) {
        return badRequest("tags must be a comma-separated list of non-empty names");
      }
      if (!validTagName(t)) {
        return badRequest("tag names must be 1-255 bytes of valid UTF-8 without control characters");
      }
      if (std::ranges::find(tags, t) == tags.end()) {
        tags.emplace_back(t);
      }
      if (comma == std::string_view::npos) {
        break;
      }
      rest.remove_prefix(comma + 1);
    }
  }
  if (req.url_params.get("tags") && tagParams.empty()) {
    return badRequest("tags must not be empty");
  }
  if (untagged && !tags.empty()) {
    return badRequest("untagged=true cannot be combined with tags");
  }

  imager::Imager::ImageList items;
  try {
    items = untagged       ? imager.getUntaggedImagesChecked(offset, limit)
            : tags.empty() ? imager.listImagesChecked(offset, limit)
                           : imager.getImagesByTagsChecked(tags, offset, limit);
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "GET /items: listing threw: " << e.what();
    return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
  }
  if (!items) {
    CROW_LOG_ERROR << "GET /items: listing failed (database error)";
    return errorResponse(items.error(), "internal error");
  }

  std::vector<crow::json::wvalue> arr;
  arr.reserve(items->size());
  for (const auto& it : *items) {
    arr.push_back(imageToJson(it));
  }
  crow::json::wvalue body;
  body["items"] = std::move(arr);
  body["offset"] = offset;
  body["limit"] = limit;
  return jsonOk(200, body);
}

crow::response storeUpload(imager::Imager& imager, const crow::request& req, Spool* spool) {
  std::string filename = req.get_header_value("X-Filename");
  if (filename.empty()) {
    if (const char* q = req.url_params.get("filename")) {
      filename = q;
    }
  }
  filename = baseName(std::move(filename));
  if (filename.empty() || filename == "." || filename == "..") {
    return badRequest("missing filename (X-Filename header or ?filename=)");
  }
  if (filename.size() > 255 ||
      std::ranges::any_of(filename, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; })) {
    return badRequest("filename must be at most 255 bytes without control characters");
  }
  // Fallback (no spool: the body is already in req.body, at most 64 KiB): spool it here.
  std::optional<Spool> fallback;
  if (spool == nullptr) {
    fallback.emplace();
    if (!fallback->ok() || fallback->write(req.body.data(), req.body.size()) != Spool::Result::Ok) {
      CROW_LOG_ERROR << "PUT /items: cannot spool upload to temp file";
      return errorResponse(imager::ErrorCode::StorageError, "internal storage error");
    }
    spool = &*fallback;
  }

  if (spool->size() == 0) {
    return badRequest("empty request body");
  }
  if (!spool->finish()) {
    CROW_LOG_ERROR << "PUT /items: cannot flush spooled upload";
    return errorResponse(imager::ErrorCode::StorageError, "internal storage error");
  }

  imager::AddResult r;
  try {
    r = imager.addFile(spool->path(), filename);
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "PUT /items: addFile threw: " << e.what();
    return errorResponse(imager::ErrorCode::StorageError, "internal error");
  }

  using imager::ErrorCode;
  switch (r.code) {
    case ErrorCode::Ok:
    case ErrorCode::DuplicateFile: {
      const bool dup = r.code == ErrorCode::DuplicateFile;
      std::string id = r.id;
      if (id.empty() && dup && r.message.size() >= ID_LEN) {
        // Some facade paths report the duplicate id only in the message tail.
        id = r.message.substr(r.message.size() - ID_LEN);
      }
      crow::json::wvalue body;
      body["id"] = std::move(id);
      body["duplicate"] = dup;
      return jsonOk(dup ? 200 : 201, body);
    }
    case ErrorCode::StorageError:
    case ErrorCode::DatabaseError:
    case ErrorCode::ConfigError:
      CROW_LOG_ERROR << "PUT /items: " << errorCodeName(r.code) << ": " << r.message;
      return errorResponse(r.code, "internal error");
    default:
      return errorResponse(r.code, r.message);
  }
}

crow::response putItem(imager::Imager& imager, const crow::request& req) {
  // The request policy (server/Upload) streams an authenticated body into a spool file, bounded by max_upload_mb.
  // Remove it before the response is sent, on every path (a bad filename, an empty body, a duplicate, ...).
  Spool* spool = uploadSpool(req);

  struct Cleanup {
    Spool* s;

    ~Cleanup() {
      if (s != nullptr) {
        s->discard();
      }
    }
  } cleanup{spool};

  return storeUpload(imager, req, spool);
}

/// Upper bound on bytes buffered for one 206 response (a shorter range than requested is legal).
constexpr uint64_t MAX_RANGE_BYTES = 8ULL << 20;

const char* contentType(const fs::path& p) {
  std::string e = p.extension().string();
  std::ranges::transform(e, e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (e == ".jpg" || e == ".jpeg") {
    return "image/jpeg";
  }
  if (e == ".png") {
    return "image/png";
  }
  if (e == ".heic" || e == ".heif") {
    return "image/heic";
  }
  if (e == ".nef") {
    return "image/x-nikon-nef";
  }
  if (e == ".mov") {
    return "video/quicktime";
  }
  if (e == ".mp4") {
    return "video/mp4";
  }
  if (e == ".aae") {
    return "application/xml";
  }
  return "application/octet-stream";
}

/// True if If-None-Match lists `"id"` (weak prefix tolerated) or `*`.
bool etagMatches(std::string_view header, std::string_view id) {
  while (!header.empty()) {
    auto comma = header.find(',');
    std::string_view tok = header.substr(0, comma);
    header = comma == std::string_view::npos ? std::string_view{} : header.substr(comma + 1);
    while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) {
      tok.remove_prefix(1);
    }
    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) {
      tok.remove_suffix(1);
    }
    if (tok.starts_with("W/")) {
      tok.remove_prefix(2);
    }
    if (tok == "*") {
      return true;
    }
    if (tok.size() == id.size() + 2 && tok.front() == '"' && tok.back() == '"' && tok.substr(1, id.size()) == id) {
      return true;
    }
  }
  return false;
}

enum class RangeKind {
  None,
  Ok,
  Unsatisfiable,
};

/// Parses a single `bytes=` range. Malformed / multi-range -> None (serve full 200).
RangeKind parseRange(std::string_view h, uint64_t size, uint64_t& first, uint64_t& last) {
  if (!h.starts_with("bytes=")) {
    return RangeKind::None;
  }
  h.remove_prefix(6);
  auto dash = h.find('-');
  if (dash == std::string_view::npos || h.find(',') != std::string_view::npos) {
    return RangeKind::None;
  }
  std::string_view a = h.substr(0, dash), b = h.substr(dash + 1);
  auto num = [](std::string_view v, uint64_t& out) {
    auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    return ec == std::errc{} && p == v.data() + v.size();
  };
  uint64_t x = 0, y = 0;
  if (a.empty()) { // suffix: last n bytes
    if (b.empty() || !num(b, y)) {
      return RangeKind::None;
    }
    if (y == 0 || size == 0) {
      return RangeKind::Unsatisfiable;
    }
    first = y >= size ? 0 : size - y;
    last = size - 1;
    return RangeKind::Ok;
  }
  if (!num(a, x)) {
    return RangeKind::None;
  }
  if (b.empty()) {
    y = UINT64_MAX;
  } else if (!num(b, y) || y < x) {
    return RangeKind::None;
  }
  if (x >= size) {
    return RangeKind::Unsatisfiable;
  }
  first = x;
  last = std::min(y, size - 1);
  return RangeKind::Ok;
}

crow::response getContent(imager::Imager& imager, const crow::request& req, const std::string& id) {
  std::optional<fs::path> path;
  try {
    path = imager.getImagePath(id);
  } catch (const std::exception& e) {
    CROW_LOG_ERROR << "GET /items/content: getImagePath threw: " << e.what();
    return errorResponse(imager::ErrorCode::StorageError, "internal error");
  }
  if (!path) {
    return errorResponse(imager::ErrorCode::FileNotFound, "item not found");
  }
  const std::string etag = "\"" + id + "\"";
  if (etagMatches(req.get_header_value("If-None-Match"), id)) {
    crow::response res{304};
    res.set_header("ETag", etag);
    res.set_header("Cache-Control", "private, immutable");
    return res;
  }
  std::error_code ec;
  const uint64_t size = fs::file_size(*path, ec);
  if (ec) {
    return errorResponse(imager::ErrorCode::FileNotFound, "item file not available");
  }

  uint64_t first = 0, last = 0;
  const RangeKind rk = parseRange(req.get_header_value("Range"), size, first, last);
  crow::response res;
  if (rk == RangeKind::Unsatisfiable) {
    res.code = 416;
    res.set_header("Content-Range", "bytes */" + std::to_string(size));
    res.set_header("Accept-Ranges", "bytes");
    return res;
  }
  if (rk == RangeKind::Ok) {
    last = std::min(last, first + MAX_RANGE_BYTES - 1);
    std::string buf(last - first + 1, '\0');
    std::ifstream f(*path, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(first));
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (static_cast<size_t>(f.gcount()) != buf.size()) {
      return errorResponse(imager::ErrorCode::StorageError, "internal error");
    }
    res.code = 206;
    res.body = std::move(buf);
    res.set_header(
      "Content-Range", "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(size)
    );
  } else {
    res.set_static_file_info_unsafe(path->string()); // streams in 16 KiB chunks
    if (res.code != 200) {
      return errorResponse(imager::ErrorCode::FileNotFound, "item file not available");
    }
  }
  res.set_header("Content-Type", contentType(*path));
  res.set_header("Accept-Ranges", "bytes");
  res.set_header("ETag", etag);
  res.set_header("Cache-Control", "private, immutable");
  return res;
}

} // namespace

void registerItemRoutes(crow::App<AuthMiddleware>& app, imager::Imager& imager) {
  CROW_ROUTE(app, "/items").methods(crow::HTTPMethod::Put)([&app, &imager](const crow::request& req) {
    if (!requireAuth(app, req)) {
      return unauthorized("authentication required");
    }
    return putItem(imager, req);
  });

  CROW_ROUTE(app, "/items").methods(crow::HTTPMethod::Get)([&app, &imager](const crow::request& req) {
    if (!requireAuth(app, req)) {
      return unauthorized("authentication required");
    }
    return listItems(imager, req);
  });

  CROW_ROUTE(app, "/items/<string>")
    .methods(crow::HTTPMethod::Get)([&app, &imager](const crow::request& req, const std::string& id) {
      if (!requireAuth(app, req)) {
        return unauthorized("authentication required");
      }
      if (!isLowerHex64(id)) {
        return badRequest("id must be 64 lowercase hex characters");
      }
      std::optional<imager::ImageInfo> info;
      try {
        info = imager.getImage(id);
      } catch (const std::exception& e) {
        CROW_LOG_ERROR << "GET /items: getImage threw: " << e.what();
        return errorResponse(imager::ErrorCode::DatabaseError, "internal error");
      }
      if (!info) {
        return errorResponse(imager::ErrorCode::FileNotFound, "item not found");
      }
      crow::json::wvalue body = imageToJson(*info);
      return jsonOk(200, body);
    });

  CROW_ROUTE(app, "/items/<string>/content")
    .methods(crow::HTTPMethod::Get)([&app, &imager](const crow::request& req, const std::string& id) {
      if (!requireAuth(app, req)) {
        return unauthorized("authentication required");
      }
      if (!isLowerHex64(id)) {
        return badRequest("id must be 64 lowercase hex characters");
      }
      return getContent(imager, req, id);
    });

  CROW_ROUTE(app, "/items/<string>")
    .methods(crow::HTTPMethod::Delete)([&app, &imager](const crow::request& req, const std::string& id) {
      if (!requireAuth(app, req)) {
        return unauthorized("authentication required");
      }
      if (!isLowerHex64(id)) {
        return badRequest("id must be 64 lowercase hex characters");
      }
      imager::ErrorCode rc;
      try {
        rc = imager.deleteImage(id);
      } catch (const std::exception& e) {
        CROW_LOG_ERROR << "DELETE /items: deleteImage threw: " << e.what();
        return errorResponse(imager::ErrorCode::StorageError, "internal error");
      }
      if (rc == imager::ErrorCode::Ok) {
        return crow::response{204};
      }
      return errorResponse(rc, rc == imager::ErrorCode::FileNotFound ? "item not found" : "internal error");
    });
}

} // namespace server
