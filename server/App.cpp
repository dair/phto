#include "App.h"

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <imager/Imager.h>

#include <thread>

#include "AuthRoutes.h"
#include "ItemRoutes.h"
#include "Json.h"
#include "TagRoutes.h"
#include "UserRoutes.h"

namespace server {

App::App(
  const config::ServerConfig& cfg,
  imager::Imager& imager,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  uint32_t tokenTtlSeconds,
  uint32_t pbkdf2Iterations
)
  : m_cfg(cfg),
    m_imager(imager),
    m_userStore(userStore),
    m_tokenService(tokenService),
    m_tokenTtlSeconds(tokenTtlSeconds),
    m_pbkdf2Iterations(pbkdf2Iterations) {
  // Wire TokenService into the middleware so it can verify tokens.
  m_crow.get_middleware<AuthMiddleware>().service = &m_tokenService;
  m_crow.get_middleware<AuthMiddleware>().users = &m_userStore;
}

App::~App() = default;

void App::registerRoutes() {
  CROW_ROUTE(m_crow, "/health")
  ([]() {
    crow::response res{200, R"({"status":"ok"})"};
    res.add_header("Content-Type", "application/json");
    return res;
  });

  // GET /stats — library size counters (authenticated).
  CROW_ROUTE(m_crow, "/stats").methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
    if (!requireAuth(m_crow, req)) {
      return unauthorized("authentication required");
    }
    try {
      const auto images = m_imager.imageCountChecked();
      const auto tags = m_imager.tagCountChecked();
      if (!images || !tags) {
        CROW_LOG_ERROR << "GET /stats: count failed (database error)";
        return internalError();
      }
      crow::json::wvalue body;
      body["imageCount"] = *images;
      body["tagCount"] = *tags;
      return jsonOk(200, body);
    } catch (const std::exception& e) {
      CROW_LOG_ERROR << "GET /stats threw: " << e.what();
      return internalError();
    }
  });

  registerAuthRoutes(m_crow, m_userStore, m_tokenService, m_tokenTtlSeconds, m_pbkdf2Iterations);
  registerUserRoutes(m_crow, m_userStore, m_pbkdf2Iterations);
  registerItemRoutes(m_crow, m_imager, m_cfg.maxUploadBytes);
  registerTagRoutes(m_crow, m_imager);
}

void App::run() {
  uint32_t threads = m_cfg.threads ? m_cfg.threads : std::thread::hardware_concurrency();
  m_crow.bindaddr(m_cfg.bind).port(m_cfg.port).multithreaded().concurrency(threads).run();
}

void App::stop() {
  m_crow.stop();
}

} // namespace server
