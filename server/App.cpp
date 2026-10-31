#include "App.h"

#include <auth/TokenService.h>
#include <auth/UserStore.h>
#include <imager/Imager.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/stat.h>

#include <stdexcept>
#include <thread>

#include "AuthRoutes.h"
#include "ItemRoutes.h"
#include "Json.h"
#include "TagRoutes.h"
#include "Upload.h"
#include "UserRoutes.h"

namespace server {

App::App(
  const config::ServerConfig& cfg,
  imager::Imager& imager,
  auth::UserStore& userStore,
  auth::TokenService& tokenService,
  const config::AuthConfig& authCfg
)
  : m_cfg(cfg),
    m_imager(imager),
    m_userStore(userStore),
    m_tokenService(tokenService),
    m_authCfg(authCfg),
    m_proxies(TrustedProxies::parse(cfg.trustedProxies)), // throws std::invalid_argument on a bad entry
    m_log(cfg.accessLog, m_proxies) {
  // Wire TokenService into the middleware so it can verify tokens.
  auto& mw = m_crow.get_middleware<AuthMiddleware>();
  mw.service = &m_tokenService;
  mw.users = &m_userStore;
  mw.accessLog = &m_log;
  mw.metrics = &m_metrics;
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
    const auto claims = requireAuth(m_crow, req);
    if (!claims) {
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
      if (claims->isAdmin) {
        body["server"] = serverJson(m_metrics.snapshot());
        body["library"] = libraryJson(m_imager.metrics().snapshot());
      }
      return jsonOk(200, body);
    } catch (const std::exception& e) {
      CROW_LOG_ERROR << "GET /stats threw: " << e.what();
      return internalError();
    }
  });

  // GET /metrics — Prometheus text exposition format 0.0.4 (admin Bearer token).
  CROW_ROUTE(m_crow, "/metrics").methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
    crow::response res;
    if (!requireAdmin(m_crow, req, res)) {
      return res;
    }
    try {
      crow::response ok{200, prometheusText(m_metrics.snapshot(), m_imager.metrics().snapshot())};
      ok.add_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
      return ok;
    } catch (const std::exception& e) {
      CROW_LOG_ERROR << "GET /metrics threw: " << e.what();
      return internalError();
    }
  });

  registerAuthRoutes(m_crow, m_userStore, m_tokenService, m_authCfg, m_proxies);
  registerUserRoutes(m_crow, m_userStore, m_authCfg.pbkdf2Iterations);
  registerItemRoutes(m_crow, m_imager);
  registerTagRoutes(m_crow, m_imager);
  installRequestPolicy(m_crow, m_cfg.maxUploadBytes, m_log);
}

namespace {

std::string sslError() {
  char buf[256] = {};
  const unsigned long e = ERR_get_error();
  if (e == 0) {
    return "unknown OpenSSL error";
  }
  ERR_error_string_n(e, buf, sizeof buf);
  return buf;
}

} // namespace

void App::enableTls() {
  if (!m_cfg.tls) {
    return;
  }
  if (m_cfg.tlsCert.empty() || m_cfg.tlsKey.empty()) {
    throw std::runtime_error("tls = true requires [server].tls_cert and [server].tls_key");
  }
  struct stat st{};
  if (::stat(m_cfg.tlsCert.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    throw std::runtime_error("tls_cert is missing or not a regular file: " + m_cfg.tlsCert.string());
  }
  if (::stat(m_cfg.tlsKey.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    throw std::runtime_error("tls_key is missing or not a regular file: " + m_cfg.tlsKey.string());
  }
  // Same stance as jwt_secret_file: a private key must not be world-readable.
  if ((st.st_mode & S_IROTH) != 0) {
    throw std::runtime_error("tls_key must not be readable by others (chmod 600): " + m_cfg.tlsKey.string());
  }

  // Own context instead of Crow's ssl_file(): lets us set the minimum protocol and
  // report OpenSSL errors. Chain loader accepts both a single cert and a full chain.
  asio::ssl::context ctx{asio::ssl::context::tls_server};
  SSL_CTX* h = ctx.native_handle();
  ctx.set_options(
    asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3
  );
  if (SSL_CTX_set_min_proto_version(h, TLS1_2_VERSION) != 1) {
    throw std::runtime_error("cannot set minimum TLS version: " + sslError());
  }
  ERR_clear_error();
  if (SSL_CTX_use_certificate_chain_file(h, m_cfg.tlsCert.c_str()) != 1) {
    throw std::runtime_error("cannot load tls_cert " + m_cfg.tlsCert.string() + ": " + sslError());
  }
  if (SSL_CTX_use_PrivateKey_file(h, m_cfg.tlsKey.c_str(), SSL_FILETYPE_PEM) != 1) {
    throw std::runtime_error("cannot load tls_key " + m_cfg.tlsKey.string() + ": " + sslError());
  }
  if (SSL_CTX_check_private_key(h) != 1) {
    throw std::runtime_error("tls_key does not match tls_cert: " + sslError());
  }
  m_crow.ssl(std::move(ctx));
}

void App::run() {
  uint32_t threads = m_cfg.threads ? m_cfg.threads : std::thread::hardware_concurrency();
  m_crow.bindaddr(m_cfg.bind).port(m_cfg.port).multithreaded().concurrency(threads).signal_clear().run();
}

void App::stop() {
  m_crow.stop();
}

} // namespace server
