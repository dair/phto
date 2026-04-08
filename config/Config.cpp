#include "Config.h"

#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>
#include <unordered_set>

namespace config {

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

  return cfg;
}

} // namespace config
