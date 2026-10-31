#include "ItemRoutes.h"

#include <imager/Imager.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "AuthRoutes.h"
#include "Json.h"

namespace server {

namespace {

namespace fs = std::filesystem;

constexpr size_t ID_LEN = 64;

/// Owns a spooled temp file; unlinks it on destruction (all paths).
class TempFile {
public:
  /// Creates `<tmpdir>/phto-upload-XXXXXX`. Check ok() before use.
  TempFile() {
    std::error_code ec;
    std::string tpl = (fs::temp_directory_path(ec) / "phto-upload-XXXXXX").string();
    if (ec) {
      return;
    }
    int fd = ::mkstemp(tpl.data());
    if (fd < 0) {
      return;
    }
    ::close(fd);
    m_path = std::move(tpl);
  }

  ~TempFile() {
    if (!m_path.empty()) {
      std::error_code ec;
      fs::remove(m_path, ec);
    }
  }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  bool ok() const noexcept {
    return !m_path.empty();
  }

  const fs::path& path() const noexcept {
    return m_path;
  }

  bool write(const std::string& data) const {
    std::ofstream f(m_path, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.close();
    return static_cast<bool>(f);
  }

private:
  fs::path m_path;
};

bool isLowerHex64(std::string_view s) {
  return s.size() == ID_LEN &&
         std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

/// Strip any directory components (both separators) from a client-supplied name.
std::string baseName(std::string name) {
  auto pos = name.find_last_of("/\\");
  return pos == std::string::npos ? name : name.substr(pos + 1);
}

crow::response jsonOk(int status, const crow::json::wvalue& body) {
  crow::response res{status, body.dump()};
  res.add_header("Content-Type", "application/json");
  return res;
}

crow::response putItem(imager::Imager& imager, uint64_t maxUploadBytes, const crow::request& req) {
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
  if (req.body.empty()) {
    return badRequest("empty request body");
  }
  if (maxUploadBytes != 0 && req.body.size() > maxUploadBytes) {
    return errorResponse(imager::ErrorCode::TooLarge, "upload exceeds max_upload_mb");
  }

  TempFile tmp;
  if (!tmp.ok() || !tmp.write(req.body)) {
    CROW_LOG_ERROR << "PUT /items: cannot spool upload to temp file";
    return errorResponse(imager::ErrorCode::StorageError, "internal storage error");
  }

  imager::AddResult r;
  try {
    r = imager.addFile(tmp.path(), filename);
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

} // namespace

void registerItemRoutes(crow::App<AuthMiddleware>& app, imager::Imager& imager, uint64_t maxUploadBytes) {
  CROW_ROUTE(app, "/items").methods(crow::HTTPMethod::Put)([&app, &imager, maxUploadBytes](const crow::request& req) {
    if (!requireAuth(app, req)) {
      return unauthorized("authentication required");
    }
    return putItem(imager, maxUploadBytes, req);
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
      crow::json::wvalue body;
      body["id"] = info->id;
      body["name"] = info->name;
      body["size"] = info->size;
      body["ext"] = info->ext;
      auto tags = crow::json::wvalue::list();
      for (const auto& t : info->tags) {
        tags.emplace_back(t);
      }
      body["tags"] = std::move(tags);
      return jsonOk(200, body);
    });
}

} // namespace server
