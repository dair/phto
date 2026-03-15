#pragma once

#include <filesystem>
#include <vector>

namespace config {

struct StorageConfig {
    std::vector<std::filesystem::path> roots; ///< At least one required
};

struct DatabaseConfig {
    std::filesystem::path path;
};

struct AppConfig {
    StorageConfig  storage;
    DatabaseConfig database;
};

/// Parse config from a TOML file. Throws std::runtime_error on missing/invalid fields.
AppConfig loadConfig(const std::filesystem::path& configPath);

} // namespace config
