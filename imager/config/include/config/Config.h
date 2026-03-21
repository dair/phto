#pragma once

#include <filesystem>
#include <vector>

namespace config {

struct TargetConfig {
    std::filesystem::path root;     ///< Storage root directory
    std::filesystem::path database; ///< SQLite database file path
};

struct AppConfig {
    std::vector<TargetConfig> targets; ///< At least one required
};

/// Parse config from a TOML file. Throws std::runtime_error on missing/invalid fields.
AppConfig loadConfig(const std::filesystem::path& configPath);

} // namespace config
