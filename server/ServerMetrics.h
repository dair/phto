#pragma once

#include <crow.h>
#include <metrics/Metrics.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server {

/// Request-level metrics of imagerd, built on the lock-free metrics:: primitives. Recording is a handful of relaxed
/// atomic adds. Label cardinality is bounded by construction: methods are a fixed set, routes are the fixed
/// templates below (anything else is "other"), status classes are 2xx..5xx. Nothing user-identifying is recorded.
class ServerMetrics {
public:
  static constexpr size_t NUM_METHODS = 8;
  static constexpr size_t NUM_CLASSES = 4;
  static constexpr int64_t NO_LATENCY = -1;

  ServerMetrics();

  ServerMetrics(const ServerMetrics&) = delete;
  ServerMetrics& operator=(const ServerMetrics&) = delete;

  /// Number of route templates including the trailing "other".
  static size_t routeCount() noexcept;
  static std::string_view routeTemplate(size_t route) noexcept;
  /// Index of the route template matching `path` (query-less), or the "other" index.
  static size_t classify(std::string_view path) noexcept;
  static std::string_view methodName(size_t method) noexcept;
  static std::string_view statusClassName(size_t cls) noexcept;

  void beginRequest() noexcept {
    m_inFlight.increment();
  }

  void endRequest() noexcept {
    m_inFlight.decrement();
  }

  /// Counts one finished response. `latencyNs` < 0 (NO_LATENCY) skips the latency histogram (early rejects).
  /// Also derives rejected_total (401/413/429) and logins_total (POST /auth/login: 200 success; 401/403 failure).
  void record(crow::HTTPMethod method, std::string_view path, int status, int64_t latencyNs) noexcept;

  /// Bytes accepted into upload spools (including those of uploads later rejected by validation).
  void addUploadBytes(uint64_t n) noexcept {
    m_uploadBytes.add(n);
  }

  struct RequestCount {
    std::string_view method, route, statusClass;
    uint64_t count;
  };

  struct Snapshot {
    uint64_t uptimeSeconds{0};
    int64_t inFlight{0};
    uint64_t uploadBytes{0};
    uint64_t rejectedTooLarge{0}, rejectedUnauthorized{0}, rejectedThrottled{0};
    uint64_t loginSuccess{0}, loginFailure{0};
    uint64_t rssBytes{0}, peakRssBytes{0};                                          // 0 when /proc is unavailable
    std::vector<RequestCount> requests;                                             // non-zero series only
    std::vector<std::pair<std::string_view, metrics::Histogram::Snapshot>> latency; // routes with samples only
  };

  Snapshot snapshot() const;

private:
  std::chrono::steady_clock::time_point m_start;
  std::deque<metrics::Counter> m_requests;  // [method][route][class], flattened
  std::deque<metrics::Histogram> m_latency; // per route
  metrics::Gauge m_inFlight{"http_requests_in_flight"};
  metrics::Counter m_uploadBytes{"upload_bytes"};
  metrics::Counter m_tooLarge{"rejected_too_large"};
  metrics::Counter m_unauthorized{"rejected_unauthorized"};
  metrics::Counter m_throttled{"rejected_throttled"};
  metrics::Counter m_loginOk{"login_success"};
  metrics::Counter m_loginFail{"login_failure"};
};

/// JSON for GET /stats "server" (snake_case, stable field names).
crow::json::wvalue serverJson(const ServerMetrics::Snapshot& s);

/// JSON for GET /stats "library" (Imager::metrics() snapshot).
crow::json::wvalue libraryJson(const metrics::FullSnapshot& s);

/// Prometheus text exposition format 0.0.4 for both snapshots.
std::string prometheusText(const ServerMetrics::Snapshot& s, const metrics::FullSnapshot& lib);

} // namespace server
