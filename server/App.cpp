#include "App.h"

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <imager/Imager.h>

#include <thread>

namespace server {

App::App(
  const config::ServerConfig& cfg, imager::Imager& imager, auth::UserStore& userStore, auth::TokenService& tokenService
)
  : m_cfg(cfg),
    m_imager(imager),
    m_userStore(userStore),
    m_tokenService(tokenService) {
  // m_imager, m_userStore, m_tokenService are used in later route-handler checkpoints.
  (void)m_imager;
  (void)m_userStore;
  (void)m_tokenService;
}

App::~App() = default;

void App::registerRoutes() {
  CROW_ROUTE(m_crow, "/health")
  ([]() {
    crow::response res{200, R"({"status":"ok"})"};
    res.add_header("Content-Type", "application/json");
    return res;
  });
}

void App::run() {
  uint32_t threads = m_cfg.threads ? m_cfg.threads : std::thread::hardware_concurrency();
  m_crow.bindaddr(m_cfg.bind).port(m_cfg.port).multithreaded().concurrency(threads).run();
}

void App::stop() {
  m_crow.stop();
}

} // namespace server
