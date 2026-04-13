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

  return cfg;
}

} // namespace config
