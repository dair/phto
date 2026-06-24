#include "App.h"

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <imager/Imager.h>

#include <thread>

#include "AuthRoutes.h"

namespace server {

App::App(
  const config::ServerConfig& cfg,
  imager::Imager& imager,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  uint32_t tokenTtlSeconds
)
  : m_cfg(cfg),
    m_imager(imager),
    m_userStore(userStore),
    m_tokenService(tokenService),
    m_tokenTtlSeconds(tokenTtlSeconds) {
  // Wire TokenService into the middleware so it can verify tokens.
  m_crow.get_middleware<AuthMiddleware>().service = &m_tokenService;
  (void)m_imager;
}

App::~App() = default;

void App::registerRoutes() {
  CROW_ROUTE(m_crow, "/health")
  ([]() {
    crow::response res{200, R"({"status":"ok"})"};
    res.add_header("Content-Type", "application/json");
    return res;
  });

  registerAuthRoutes(m_crow, m_userStore, m_tokenService, m_tokenTtlSeconds);
}

void App::run() {
  uint32_t threads = m_cfg.threads ? m_cfg.threads : std::thread::hardware_concurrency();
  m_crow.bindaddr(m_cfg.bind).port(m_cfg.port).multithreaded().concurrency(threads).run();
}

void App::stop() {
  m_crow.stop();
}

} // namespace server
