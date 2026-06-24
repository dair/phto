#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <config/Config.h>
#include <imager/Imager.h>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <string>

#include "App.h"

namespace {

// Global pointer set before signal installation; signal handler calls stop().
server::App* g_app = nullptr;

void handleSignal(int /*sig*/) {
  if (g_app) {
    g_app->stop();
  }
}

/// Load JWT secret: prefer jwtSecretFile, fall back to jwtSecret inline.
/// Returns the secret string, or empty on error (after printing to stderr).
std::string loadJwtSecret(const config::AuthConfig& auth) {
  if (!auth.jwtSecretFile.empty()) {
    std::ifstream f(auth.jwtSecretFile);
    if (!f) {
      std::cerr << "imagerd: cannot open jwt_secret_file: " << auth.jwtSecretFile << "\n";
      return {};
    }
    std::string secret{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    // Trim a single trailing newline (common in secret files written by echo/text editors).
    if (!secret.empty() && secret.back() == '\n') {
      secret.pop_back();
    }
    return secret;
  }
  return auth.jwtSecret;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: imagerd <config.toml>\n";
    return 1;
  }

  // --- Load config ---
  config::AppConfig cfg;
  try {
    cfg = config::loadConfig(argv[1]);
  } catch (const std::exception& e) {
    std::cerr << "imagerd: config error: " << e.what() << "\n";
    return 2;
  }

  // --- Load + validate JWT secret ---
  const std::string jwtSecret = loadJwtSecret(cfg.auth);
  if (jwtSecret.size() < 32) {
    std::cerr << "imagerd: JWT secret must be >= 32 bytes (got " << jwtSecret.size() << "); "
              << "set [auth].jwt_secret_file or [auth].jwt_secret in config\n";
    return 2;
  }

  // --- Construct shared services ---
  imager::Imager imager{cfg};
  auth::UserStore userStore{cfg.auth.database};
  auth::TokenService tokenService{jwtSecret, cfg.auth.issuer, cfg.auth.tokenTtlSeconds};

  // --- Build App + register routes ---
  server::App app{cfg.server, imager, userStore, tokenService, cfg.auth.tokenTtlSeconds, cfg.auth.pbkdf2Iterations};
  app.registerRoutes();

  // --- Install signal handlers ---
  g_app = &app;
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  std::cerr << "imagerd: listening on " << cfg.server.bind << ":" << cfg.server.port << "\n";

  app.run();

  std::cerr << "imagerd: shutdown complete\n";
  return 0;
}
