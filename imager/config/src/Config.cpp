#include "config/Config.h"

#include <toml++/toml.hpp>

#include <stdexcept>
#include <string>

namespace config {

AppConfig loadConfig(const std::filesystem::path& configPath) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(configPath.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            std::string("Failed to parse config file '") + configPath.string()
            + "': " + e.what());
    }

    AppConfig cfg;

    // [storage].roots — required, non-empty array of strings
    auto* roots = tbl["storage"]["roots"].as_array();
    if (!roots || roots->empty()) {
        throw std::runtime_error(
            "Config: [storage].roots must be a non-empty array of path strings");
    }
    for (const auto& el : *roots) {
        const auto* sv = el.as_string();
        if (!sv) {
            throw std::runtime_error(
                "Config: each entry in [storage].roots must be a string");
        }
        cfg.storage.roots.emplace_back(sv->get());
    }

    // [database].path — required string
    auto dbPath = tbl["database"]["path"].value<std::string>();
    if (!dbPath) {
        throw std::runtime_error("Config: [database].path is required");
    }
    cfg.database.path = *dbPath;

    return cfg;
}

} // namespace config
