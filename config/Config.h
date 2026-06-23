#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace config {

struct TargetConfig {
  std::filesystem::path root;     ///< Storage root directory
  std::filesystem::path database; ///< SQLite database file path
};

struct ServerConfig {
  std::string bind = "0.0.0.0";                        ///< [server].bind
  uint16_t port = 8443;                                ///< [server].port
  bool tls = false;                                    ///< [server].tls
  std::filesystem::path tlsCert;                       ///< [server].tls_cert
  std::filesystem::path tlsKey;                        ///< [server].tls_key
  uint32_t threads = 0;                                ///< [server].threads (0 = hardware_concurrency)
  uint64_t maxUploadBytes = 4ULL * 1024 * 1024 * 1024; ///< [server].max_upload_mb (MB -> bytes)
};

struct AuthConfig {
  std::filesystem::path database;      ///< [auth].database
  std::string jwtSecret;               ///< [auth].jwt_secret (inline, discouraged)
  std::filesystem::path jwtSecretFile; ///< [auth].jwt_secret_file (preferred)
  uint32_t tokenTtlSeconds = 43200;    ///< [auth].token_ttl_seconds (12h)
  std::string issuer = "phto";         ///< [auth].issuer
  uint32_t pbkdf2Iterations = 310000;  ///< [auth].pbkdf2_iterations
};

struct AppConfig {
  std::vector<TargetConfig> targets; ///< At least one required

  /// Per-extension maximum file size in bytes.
  /// Key: lowercase extension without dot (e.g. "jpg", "mov").
  /// Value: maximum size in bytes. Files exceeding their format's limit are
  /// rejected without processing. Populated with compiled defaults merged with
  /// any user overrides from the [file_size_limits] config section.
  std::unordered_map<std::string, uint64_t> fileSizeLimits;

  ServerConfig server; ///< [server] section (optional; offline tools ignore it)
  AuthConfig auth;     ///< [auth] section (optional; offline tools ignore it)
};

/// Parse config from a TOML file. Throws std::runtime_error on missing/invalid fields.
AppConfig loadConfig(const std::filesystem::path& configPath);

} // namespace config
