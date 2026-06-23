#pragma once

#include <config/Config.h>
#include <crow.h>

#include <atomic>
#include <thread>

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
    auth::TokenService& tokenService
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

private:
  const config::ServerConfig& m_cfg;
  imager::Imager& m_imager;
  auth::UserStore& m_userStore;
  auth::TokenService& m_tokenService;

  crow::SimpleApp m_crow;
};

} // namespace server
