#pragma once

#include <config/Config.h>
#include <crow.h>

#include <atomic>
#include <thread>

#include "AuthMiddleware.h"

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
    uint32_t tokenTtlSeconds,
    uint32_t pbkdf2Iterations
  );
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;
  App(App&&) = delete;
  App& operator=(App&&) = delete;

  /// Register all routes. Call before run().
  void registerRoutes();

  /// Start the server (blocking until stop() is called from another thread).
  void run();

  /// Signal graceful shutdown; safe to call from a signal handler context via
  /// std::atomic_flag / app.stop().
  void stop();

  crow::App<AuthMiddleware>& crow() {
    return m_crow;
  }

private:
  const config::ServerConfig& m_cfg;
  imager::Imager& m_imager;
  auth::UserStore& m_userStore;
  auth::TokenService& m_tokenService;
  uint32_t m_tokenTtlSeconds;
  uint32_t m_pbkdf2Iterations;

  crow::App<AuthMiddleware> m_crow;
};

} // namespace server
