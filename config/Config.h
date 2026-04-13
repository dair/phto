#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace config {

struct TargetConfig {
  std::filesystem::path root;     ///< Storage root directory
  std::filesystem::path database; ///< SQLite database file path
};

struct AppConfig {
  std::vector<TargetConfig> targets; ///< At least one required

  /// Per-extension maximum file size in bytes.
  /// Key: lowercase extension without dot (e.g. "jpg", "mov").
  /// Value: maximum size in bytes. Files exceeding their format's limit are
  /// rejected without processing. Populated with compiled defaults merged with
  /// any user overrides from the [file_size_limits] config section.
  std::unordered_map<std::string, uint64_t> fileSizeLimits;
};

/// Parse config from a TOML file. Throws std::runtime_error on missing/invalid fields.
AppConfig loadConfig(const std::filesystem::path& configPath);

} // namespace config
