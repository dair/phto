#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <config/Config.h>
#include <imager/Imager.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "App.h"
#include "Upload.h"

namespace {

/// Load JWT secret: prefer jwtSecretFile, fall back to jwtSecret inline.
/// Returns the secret string, or empty on error (after printing to stderr).
std::string loadJwtSecret(const config::AuthConfig& auth) {
  if (!auth.jwtSecretFile.empty()) {
    // Refuse a secret file that is not a regular file or is readable by "other" (should be 0600/0640).
    struct stat st{};
    if (::stat(auth.jwtSecretFile.c_str(), &st) != 0) {
      std::cerr << "imagerd: cannot stat jwt_secret_file: " << auth.jwtSecretFile << "\n";
      return {};
    }
    if (!S_ISREG(st.st_mode) || (st.st_mode & S_IROTH) != 0) {
      std::cerr << "imagerd: jwt_secret_file must be a regular file not readable by others (chmod 600): "
                << auth.jwtSecretFile << "\n";
      return {};
    }
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

  // Block SIGINT/SIGTERM in every thread (the mask is inherited by all threads created later, including the
  // Imager pool and Crow workers). They are consumed synchronously by the watcher thread below via sigwait(),
  // so no code runs in signal-handler context at all (async-signal-safe by construction).
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGINT);
  sigaddset(&sigs, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &sigs, nullptr);

  // --- Load config ---
  config::AppConfig cfg;
  try {
    cfg = config::loadConfig(argv[1]);
  } catch (const std::exception& e) {
    std::cerr << "imagerd: config error: " << e.what() << "\n";
    return 2;
  }

  if (cfg.auth.database.empty()) {
    std::cerr << "imagerd: no auth database configured (set [auth].database)\n";
    return 2;
  }

  // --- Load + validate JWT secret ---
  const std::string jwtSecret = loadJwtSecret(cfg.auth);
  if (jwtSecret.empty()) {
    std::cerr << "imagerd: no JWT secret configured (set [auth].jwt_secret_file)\n";
    return 2;
  }
  if (jwtSecret.size() < 32) {
    std::cerr << "imagerd: JWT secret must be >= 32 bytes (got " << jwtSecret.size() << ")\n";
    return 2;
  }

  // --- Construct shared services ---
  std::optional<imager::Imager> imager;
  try {
    imager.emplace(cfg);
  } catch (const std::exception& e) {
    std::cerr << "imagerd: cannot open storage: " << e.what() << "\n";
    return 2;
  }
  std::optional<auth::UserStore> userStore;
  try {
    userStore.emplace(cfg.auth.database);
  } catch (const std::exception& e) {
    std::cerr << "imagerd: cannot open auth database " << cfg.auth.database.string() << ": " << e.what() << "\n";
    return 2;
  }
  std::optional<auth::TokenService> tokenService;
  try {
    tokenService.emplace(jwtSecret, cfg.auth.issuer, cfg.auth.tokenTtlSeconds);
  } catch (const std::exception& e) {
    std::cerr << "imagerd: token service error: " << e.what() << "\n";
    return 2;
  }

  // --- Build App + register routes ---
  try {
    (void)server::TrustedProxies::parse(cfg.server.trustedProxies); // fail fast with a clear message
  } catch (const std::exception& e) {
    std::cerr << "imagerd: " << e.what() << "\n";
    return 2;
  }
  // Our access log replaces Crow's per-request INFO lines (which would also echo query strings).
  crow::logger::setLogLevel(crow::LogLevel::Warning);
  server::App app{cfg.server, *imager, *userStore, *tokenService, cfg.auth};
  app.registerRoutes();
  try {
    app.enableTls();
  } catch (const std::exception& e) {
    std::cerr << "imagerd: TLS error: " << e.what() << "\n";
    return 2;
  }

  // --- Shutdown watcher ---
  // Crow's stop() halts the io_contexts: nothing new is accepted or read, while a handler that has already
  // started (Crow writes its response, including streamed static files, synchronously inside that handler)
  // runs to completion because run() joins its worker threads. The watcher bounds that wait.
  std::mutex mu;
  std::condition_variable cv;
  bool stopped = false; // guarded by mu: app.run() has returned
  std::thread watcher([&] {
    int sig = 0;
    sigwait(&sigs, &sig);
    {
      std::lock_guard lock(mu);
      if (stopped) {
        return; // woken by main() after a normal return
      }
    }
    const std::chrono::seconds drainTimeout{cfg.server.drainTimeoutSeconds};
    std::cerr << "imagerd: signal " << sig << ", draining (up to " << drainTimeout.count() << "s)\n";
    app.stop();
    std::unique_lock lock(mu);
    if (!cv.wait_for(lock, drainTimeout, [&] { return stopped; })) {
      std::cerr << "imagerd: drain timeout, forcing exit\n";
      server::removeAllSpools(); // destructors will not run: unlink in-flight upload spool files
      std::_Exit(3);
    }
  });

  std::cerr << "imagerd: serving " << (cfg.server.tls ? "HTTPS" : "HTTP") << " on " << cfg.server.bind << ":"
            << cfg.server.port << "\n";

  int rc = 0;
  try {
    app.run();
  } catch (const std::exception& e) {
    std::cerr << "imagerd: server error: " << e.what() << "\n";
    rc = 2;
  }

  server::removeAllSpools(); // uploads cut off by shutdown (normally already removed with their connections)
  {
    std::lock_guard lock(mu);
    stopped = true;
  }
  cv.notify_all();
  pthread_kill(watcher.native_handle(), SIGTERM); // wakes sigwait() if no signal ever arrived
  watcher.join();

  if (rc == 0) {
    std::cerr << "imagerd: shutdown complete\n";
  }
  return rc;
}
