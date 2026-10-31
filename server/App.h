#pragma once

#include <config/Config.h>
#include <crow.h>

#include <atomic>
#include <thread>

#include "AccessLog.h"
#include "AuthMiddleware.h"
#include "ClientIp.h"
#include "ServerMetrics.h"

namespace auth {

class TokenService;
class UserStore;

} // namespace auth

namespace imager {

class Imager;

} // namespace imager

namespace server {

class App {
public:
  App(
    const config::ServerConfig& cfg,
    imager::Imager& imager,
    auth::UserStore& userStore,
    auth::TokenService& tokenService,
    const config::AuthConfig& authCfg
  );
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;
  App(App&&) = delete;
  App& operator=(App&&) = delete;

  /// Register all routes and the request-size policy. Call before run().
  void registerRoutes();

  /// Configure native TLS from cfg (no-op when tls = false). Requires TLS >= 1.2.
  /// Validates cert/key eagerly; throws std::runtime_error with a clear message.
  void enableTls();

  /// Start the server (blocking until stop() is called from another thread).
  void run();

  /// Stop accepting and reading; started handlers finish, then run() returns. NOT async-signal-safe: call from
  /// a normal thread (imagerd uses a sigwait() watcher thread). run() clears Crow's own SIGINT/SIGTERM handling.
  void stop();

  crow::App<AuthMiddleware>& crow() {
    return m_crow;
  }

private:
  const config::ServerConfig& m_cfg;
  imager::Imager& m_imager;
  auth::UserStore& m_userStore;
  auth::TokenService& m_tokenService;
  const config::AuthConfig& m_authCfg;

  TrustedProxies m_proxies; // before m_log / routes: both hold references to it
  AccessLog m_log;
  ServerMetrics m_metrics;
  crow::App<AuthMiddleware> m_crow;
};

} // namespace server
