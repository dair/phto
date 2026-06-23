#include "Config.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>
#include <unordered_set>

namespace config {

namespace {

static constexpr uint64_t kMegabyte = 1024ULL * 1024ULL;
static constexpr uint64_t kDefaultLossyImageSizeLimit = 250ULL * kMegabyte;
static constexpr uint64_t kDefaultLosslessImageSizeLimit = 500ULL * kMegabyte;
static constexpr uint64_t kDefaultVideoSizeLimit = 100ULL * 1024ULL * kMegabyte;

static const std::unordered_map<std::string, uint64_t> kDefaultSizeLimits = {
  {"jpg", kDefaultLossyImageSizeLimit},
  {"jpeg", kDefaultLossyImageSizeLimit},
  {"png", kDefaultLosslessImageSizeLimit},
  {"heic", kDefaultLossyImageSizeLimit},
  {"heif", kDefaultLossyImageSizeLimit},
  {"nef", kDefaultLosslessImageSizeLimit},
  {"aae", 1ULL * kMegabyte},
  {"mov", kDefaultVideoSizeLimit},
  {"mp4", kDefaultVideoSizeLimit},
};

/// Parse a human-readable size string like "250MB", "100GB", "512KB", "1024".
/// Accepted suffixes (case-insensitive): B, KB, MB, GB, TB.
/// Returns bytes. Throws std::runtime_error on parse failure.
static uint64_t parseSizeString(const std::string& s) {
  if (s.empty()) {
    throw std::runtime_error("Config [file_size_limits]: empty size string");
  }
  size_t numEnd = 0;
  while (numEnd < s.size() && (std::isdigit(static_cast<unsigned char>(s[numEnd])) || s[numEnd] == '.')) {
    ++numEnd;
  }
  if (numEnd == 0) {
    throw std::runtime_error("Config [file_size_limits]: no numeric prefix in '" + s + "'");
  }
  double num = std::stod(s.substr(0, numEnd));
  std::string suffix = s.substr(numEnd);
  // Trim leading whitespace in suffix
  while (!suffix.empty() && suffix[0] == ' ') {
    suffix = suffix.substr(1);
  }
  // Uppercase suffix for comparison
  for (auto& c : suffix) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  uint64_t multiplier = 1;
  if (suffix.empty() || suffix == "B") {
    multiplier = 1;
  } else if (suffix == "KB") {
    multiplier = 1024ULL;
  } else if (suffix == "MB") {
    multiplier = 1024ULL * 1024ULL;
  } else if (suffix == "GB") {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
  } else if (suffix == "TB") {
    multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
  } else {
    throw std::runtime_error("Config [file_size_limits]: unknown size suffix '" + suffix + "' in '" + s + "'");
  }
  return static_cast<uint64_t>(num * static_cast<double>(multiplier));
}

} // namespace

AppConfig loadConfig(const std::filesystem::path& configPath) {
  toml::table tbl;
  try {
    tbl = toml::parse_file(configPath.string());
  } catch (const toml::parse_error& e) {
    throw std::runtime_error(std::string("Failed to parse config file '") + configPath.string() + "': " + e.what());
  }

  auto* targetsArr = tbl["targets"].as_array();
  if (!targetsArr || targetsArr->empty()) {
    throw std::runtime_error("Config: [[targets]] must be a non-empty array of tables");
  }

  AppConfig cfg;
  cfg.targets.reserve(targetsArr->size());

  for (const auto& el : *targetsArr) {
    const auto* t = el.as_table();
    if (!t) {
      throw std::runtime_error("Config: each entry in [[targets]] must be a table");
    }

    auto root = (*t)["root"].value<std::string>();
    if (!root) {
      throw std::runtime_error("Config: each [[targets]] entry must have a 'root' string");
    }

    auto database = (*t)["database"].value<std::string>();
    if (!database) {
      throw std::runtime_error("Config: each [[targets]] entry must have a 'database' string");
    }

    cfg.targets.push_back({*root, *database});
  }

  // Semantic validation: duplicate root or database paths are rejected.
  {
    std::unordered_set<std::string> roots, databases;
    for (const auto& target : cfg.targets) {
      auto rootStr = target.root.string();
      if (!roots.insert(rootStr).second) {
        throw std::runtime_error("Config: duplicate root path: " + rootStr);
      }
      auto dbStr = target.database.string();
      if (!databases.insert(dbStr).second) {
        throw std::runtime_error("Config: duplicate database path: " + dbStr);
      }
    }
  }

  // Populate fileSizeLimits: start with compiled defaults then merge user overrides.
  cfg.fileSizeLimits = kDefaultSizeLimits;
  if (auto* limits = tbl["file_size_limits"].as_table()) {
    for (const auto& [key, val] : *limits) {
      auto sizeStr = val.value<std::string>();
      if (!sizeStr) {
        throw std::runtime_error(
          "Config [file_size_limits]: value for '" + std::string(key.str()) + "' must be a string"
        );
      }
      uint64_t bytes = parseSizeString(*sizeStr);
      std::string ext = std::string(key.str());
      // Lowercase the extension key
      for (auto& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      cfg.fileSizeLimits[ext] = bytes;
    }
  }

  // Parse optional [server] section.
  if (auto* serverTbl = tbl["server"].as_table()) {
    if (auto v = (*serverTbl)["bind"].value<std::string>()) {
      cfg.server.bind = *v;
    } else if ((*serverTbl)["bind"]) {
      throw std::runtime_error("Config [server]: 'bind' must be a string");
    }

    if (auto v = (*serverTbl)["port"].value<int64_t>()) {
      if (*v < 1 || *v > 65535) {
        throw std::runtime_error("Config [server]: 'port' must be in range 1..65535, got " + std::to_string(*v));
      }
      cfg.server.port = static_cast<uint16_t>(*v);
    } else if ((*serverTbl)["port"]) {
      throw std::runtime_error("Config [server]: 'port' must be an integer");
    }

    if (auto v = (*serverTbl)["tls"].value<bool>()) {
      cfg.server.tls = *v;
    } else if ((*serverTbl)["tls"]) {
      throw std::runtime_error("Config [server]: 'tls' must be a boolean");
    }

    if (auto v = (*serverTbl)["tls_cert"].value<std::string>()) {
      cfg.server.tlsCert = *v;
    } else if ((*serverTbl)["tls_cert"]) {
      throw std::runtime_error("Config [server]: 'tls_cert' must be a string");
    }

    if (auto v = (*serverTbl)["tls_key"].value<std::string>()) {
      cfg.server.tlsKey = *v;
    } else if ((*serverTbl)["tls_key"]) {
      throw std::runtime_error("Config [server]: 'tls_key' must be a string");
    }

    if (auto v = (*serverTbl)["threads"].value<int64_t>()) {
      cfg.server.threads = static_cast<uint32_t>(*v);
    } else if ((*serverTbl)["threads"]) {
      throw std::runtime_error("Config [server]: 'threads' must be an integer");
    }

    if (auto v = (*serverTbl)["max_upload_mb"].value<int64_t>()) {
      if (*v <= 0) {
        throw std::runtime_error("Config [server]: 'max_upload_mb' must be > 0, got " + std::to_string(*v));
      }
      cfg.server.maxUploadBytes = static_cast<uint64_t>(*v) * 1024ULL * 1024ULL;
    } else if ((*serverTbl)["max_upload_mb"]) {
      throw std::runtime_error("Config [server]: 'max_upload_mb' must be an integer");
    }
  }

  // Parse optional [auth] section.
  if (auto* authTbl = tbl["auth"].as_table()) {
    if (auto v = (*authTbl)["database"].value<std::string>()) {
      cfg.auth.database = *v;
    } else if ((*authTbl)["database"]) {
      throw std::runtime_error("Config [auth]: 'database' must be a string");
    }

    if (auto v = (*authTbl)["jwt_secret"].value<std::string>()) {
      cfg.auth.jwtSecret = *v;
    } else if ((*authTbl)["jwt_secret"]) {
      throw std::runtime_error("Config [auth]: 'jwt_secret' must be a string");
    }

    if (auto v = (*authTbl)["jwt_secret_file"].value<std::string>()) {
      cfg.auth.jwtSecretFile = *v;
    } else if ((*authTbl)["jwt_secret_file"]) {
      throw std::runtime_error("Config [auth]: 'jwt_secret_file' must be a string");
    }

    if (auto v = (*authTbl)["token_ttl_seconds"].value<int64_t>()) {
      if (*v <= 0) {
        throw std::runtime_error("Config [auth]: 'token_ttl_seconds' must be > 0, got " + std::to_string(*v));
      }
      cfg.auth.tokenTtlSeconds = static_cast<uint32_t>(*v);
    } else if ((*authTbl)["token_ttl_seconds"]) {
      throw std::runtime_error("Config [auth]: 'token_ttl_seconds' must be an integer");
    }

    if (auto v = (*authTbl)["issuer"].value<std::string>()) {
      cfg.auth.issuer = *v;
    } else if ((*authTbl)["issuer"]) {
      throw std::runtime_error("Config [auth]: 'issuer' must be a string");
    }

    if (auto v = (*authTbl)["pbkdf2_iterations"].value<int64_t>()) {
      if (*v < 1000) {
        throw std::runtime_error("Config [auth]: 'pbkdf2_iterations' must be >= 1000, got " + std::to_string(*v));
      }
      cfg.auth.pbkdf2Iterations = static_cast<uint32_t>(*v);
    } else if ((*authTbl)["pbkdf2_iterations"]) {
      throw std::runtime_error("Config [auth]: 'pbkdf2_iterations' must be an integer");
    }
  }

  return cfg;
}

} // namespace config
