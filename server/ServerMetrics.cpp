#include "ServerMetrics.h"

#include <charconv>
#include <cstdio>
#include <fstream>

namespace server {

namespace {

// Keep in sync with the CROW_ROUTE registrations. '{x}' matches exactly one non-empty path segment.
constexpr std::string_view ROUTES[] = {
  "/health",
  "/stats",
  "/metrics",
  "/auth/login",
  "/auth/me",
  "/auth/password",
  "/users",
  "/users/{login}",
  "/items",
  "/items/{id}",
  "/items/{id}/content",
  "/items/{id}/tags",
  "/items/{id}/tags/{tag}",
  "/tags",
  "/tags/{name}",
  "other",
};
constexpr size_t NUM_ROUTES = std::size(ROUTES);
constexpr size_t OTHER = NUM_ROUTES - 1;
constexpr size_t LOGIN_ROUTE = 3;
constexpr size_t NUM_STATUS_CLASSES = ServerMetrics::NUM_CLASSES;

constexpr std::string_view METHODS[ServerMetrics::NUM_METHODS] = {
  "GET", "PUT", "POST", "DELETE", "PATCH", "HEAD", "OPTIONS", "other"
};
constexpr std::string_view CLASSES[NUM_STATUS_CLASSES] = {"2xx", "3xx", "4xx", "5xx"};

size_t methodIndex(crow::HTTPMethod m) noexcept {
  switch (m) {
    case crow::HTTPMethod::Get:
      return 0;
    case crow::HTTPMethod::Put:
      return 1;
    case crow::HTTPMethod::Post:
      return 2;
    case crow::HTTPMethod::Delete:
      return 3;
    case crow::HTTPMethod::Patch:
      return 4;
    case crow::HTTPMethod::Head:
      return 5;
    case crow::HTTPMethod::Options:
      return 6;
    default:
      return 7;
  }
}

size_t classIndex(int status) noexcept {
  return status < 300 ? 0 : status < 400 ? 1 : status < 500 ? 2 : 3;
}

/// Takes the next '/'-delimited segment from `s` (which must start after a '/'); returns false when exhausted.
bool nextSegment(std::string_view& s, std::string_view& seg, bool& first) noexcept {
  if (first) {
    first = false;
    if (s.empty() || s.front() != '/') {
      return false;
    }
    s.remove_prefix(1);
  } else if (s.empty()) {
    return false;
  }
  const size_t p = s.find('/');
  seg = s.substr(0, p);
  s.remove_prefix(p == std::string_view::npos ? s.size() : p + 1);
  return true;
}

bool matches(std::string_view tpl, std::string_view path) noexcept {
  bool tFirst = true;
  bool pFirst = true;
  bool pTrailing = !path.empty() && path.back() == '/'; // "/items/" yields an empty last segment
  std::string_view a, b;
  for (;;) {
    const bool hasT = nextSegment(tpl, a, tFirst);
    const bool hasP = nextSegment(path, b, pFirst);
    if (hasT != hasP) {
      return false;
    }
    if (!hasT) {
      return !pTrailing;
    }
    if (b.empty()) {
      return false;
    }
    if (a.front() == '{') {
      continue;
    }
    if (a != b) {
      return false;
    }
  }
}

uint64_t procStatusKb(std::string_view key) {
  std::ifstream f{"/proc/self/status"};
  std::string line;
  while (std::getline(f, line)) {
    if (line.compare(0, key.size(), key) == 0) {
      uint64_t kb = 0;
      const char* p = line.data() + key.size();
      const char* e = line.data() + line.size();
      while (p < e && (*p == ' ' || *p == '\t')) {
        ++p;
      }
      std::from_chars(p, e, kb);
      return kb * 1024;
    }
  }
  return 0;
}

std::string fmtDouble(double v) {
  char buf[40];
  const int n = std::snprintf(buf, sizeof buf, "%.9g", v);
  return std::string{buf, static_cast<size_t>(n)};
}

double seconds(uint64_t ns) {
  return static_cast<double>(ns) / 1e9;
}

/// Cumulative bucket counts for a (per-bucket) Histogram::Snapshot. Last element is the +Inf total.
std::array<uint64_t, metrics::Histogram::NUM_BUCKETS> cumulative(const metrics::Histogram::Snapshot& h) {
  std::array<uint64_t, metrics::Histogram::NUM_BUCKETS> c{};
  uint64_t run = 0;
  for (size_t i = 0; i < c.size(); ++i) {
    run += h.buckets[i];
    c[i] = run;
  }
  return c;
}

crow::json::wvalue histogramJson(const metrics::Histogram::Snapshot& h) {
  const auto cum = cumulative(h);
  auto buckets = crow::json::wvalue::list();
  for (size_t i = 0; i < cum.size(); ++i) {
    crow::json::wvalue b;
    if (i + 1 < cum.size()) {
      b["le"] = seconds(metrics::Histogram::BOUNDS[i]);
    } else {
      b["le"] = "+Inf";
    }
    b["count"] = cum[i];
    buckets.emplace_back(std::move(b));
  }
  crow::json::wvalue v;
  v["count"] = cum.back();
  v["sum_seconds"] = seconds(h.sum_ns);
  v["buckets"] = std::move(buckets);
  return v;
}

// ---- Prometheus text ----

void escapeLabel(std::string& out, std::string_view v) {
  for (const char c : v) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '"') {
      out += "\\\"";
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
}

void family(std::string& out, std::string_view name, std::string_view type, std::string_view help) {
  out += "# HELP ";
  out += name;
  out += ' ';
  out += help;
  out += "\n# TYPE ";
  out += name;
  out += ' ';
  out += type;
  out += '\n';
}

void sample(std::string& out, std::string_view name, std::string_view labels, std::string_view value) {
  out += name;
  if (!labels.empty()) {
    out += '{';
    out += labels;
    out += '}';
  }
  out += ' ';
  out += value;
  out += '\n';
}

std::string label(std::string_view k, std::string_view v) {
  std::string s{k};
  s += "=\"";
  escapeLabel(s, v);
  s += '"';
  return s;
}

/// Histogram samples (buckets/_sum/_count) without the HELP/TYPE header. `extra` = leading label(s) or empty.
void histogramSamples(
  std::string& out, const std::string& name, const std::string& extra, const metrics::Histogram::Snapshot& h
) {
  const auto cum = cumulative(h);
  const std::string pre = extra.empty() ? "" : extra + ",";
  for (size_t i = 0; i < cum.size(); ++i) {
    const std::string le = i + 1 < cum.size() ? fmtDouble(seconds(metrics::Histogram::BOUNDS[i])) : "+Inf";
    sample(out, name + "_bucket", pre + label("le", le), std::to_string(cum[i]));
  }
  sample(out, name + "_sum", extra, fmtDouble(seconds(h.sum_ns)));
  sample(out, name + "_count", extra, std::to_string(cum.back()));
}

} // namespace

ServerMetrics::ServerMetrics()
  : m_start(std::chrono::steady_clock::now()) {
  for (size_t m = 0; m < NUM_METHODS; ++m) {
    for (size_t r = 0; r < NUM_ROUTES; ++r) {
      for (size_t c = 0; c < NUM_STATUS_CLASSES; ++c) {
        m_requests.emplace_back("requests");
      }
    }
  }
  for (size_t r = 0; r < NUM_ROUTES; ++r) {
    m_latency.emplace_back("request_duration");
  }
}

size_t ServerMetrics::routeCount() noexcept {
  return NUM_ROUTES;
}

std::string_view ServerMetrics::routeTemplate(size_t route) noexcept {
  return ROUTES[route];
}

std::string_view ServerMetrics::methodName(size_t method) noexcept {
  return METHODS[method];
}

std::string_view ServerMetrics::statusClassName(size_t cls) noexcept {
  return CLASSES[cls];
}

size_t ServerMetrics::classify(std::string_view path) noexcept {
  for (size_t i = 0; i < OTHER; ++i) {
    if (matches(ROUTES[i], path)) {
      return i;
    }
  }
  return OTHER;
}

void ServerMetrics::record(crow::HTTPMethod method, std::string_view path, int status, int64_t latencyNs) noexcept {
  const size_t route = classify(path);
  const size_t m = methodIndex(method);
  m_requests[(m * NUM_ROUTES + route) * NUM_STATUS_CLASSES + classIndex(status)].add(1);
  if (latencyNs >= 0) {
    m_latency[route].record(std::chrono::nanoseconds{latencyNs});
  }
  switch (status) {
    case 401:
      m_unauthorized.add(1);
      break;
    case 413:
      m_tooLarge.add(1);
      break;
    case 429:
      m_throttled.add(1);
      break;
    default:
      break;
  }
  if (route == LOGIN_ROUTE && method == crow::HTTPMethod::Post) {
    if (status == 200) {
      m_loginOk.add(1);
    } else if (status == 401 || status == 403) {
      m_loginFail.add(1);
    }
  }
}

ServerMetrics::Snapshot ServerMetrics::snapshot() const {
  Snapshot s;
  s.uptimeSeconds = static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_start).count()
  );
  s.inFlight = m_inFlight.value();
  s.uploadBytes = m_uploadBytes.value();
  s.rejectedTooLarge = m_tooLarge.value();
  s.rejectedUnauthorized = m_unauthorized.value();
  s.rejectedThrottled = m_throttled.value();
  s.loginSuccess = m_loginOk.value();
  s.loginFailure = m_loginFail.value();
  s.rssBytes = procStatusKb("VmRSS:");
  s.peakRssBytes = procStatusKb("VmHWM:");
  size_t i = 0;
  for (size_t m = 0; m < NUM_METHODS; ++m) {
    for (size_t r = 0; r < NUM_ROUTES; ++r) {
      for (size_t c = 0; c < NUM_STATUS_CLASSES; ++c, ++i) {
        if (const uint64_t v = m_requests[i].value(); v != 0) {
          s.requests.push_back({METHODS[m], ROUTES[r], CLASSES[c], v});
        }
      }
    }
  }
  for (size_t r = 0; r < NUM_ROUTES; ++r) {
    auto h = m_latency[r].snapshot();
    if (h.count != 0) {
      s.latency.emplace_back(ROUTES[r], std::move(h));
    }
  }
  return s;
}

crow::json::wvalue serverJson(const ServerMetrics::Snapshot& s) {
  crow::json::wvalue v;
  v["uptime_seconds"] = s.uptimeSeconds;
  v["in_flight_requests"] = s.inFlight;
  v["upload_bytes_total"] = s.uploadBytes;
  v["rejected_total"]["too_large"] = s.rejectedTooLarge;
  v["rejected_total"]["unauthorized"] = s.rejectedUnauthorized;
  v["rejected_total"]["throttled"] = s.rejectedThrottled;
  v["logins_total"]["success"] = s.loginSuccess;
  v["logins_total"]["failure"] = s.loginFailure;
  v["process"]["resident_memory_bytes"] = s.rssBytes;
  v["process"]["peak_resident_memory_bytes"] = s.peakRssBytes;
  auto reqs = crow::json::wvalue::list();
  for (const auto& r : s.requests) {
    crow::json::wvalue e;
    e["method"] = std::string{r.method};
    e["route"] = std::string{r.route};
    e["status_class"] = std::string{r.statusClass};
    e["count"] = r.count;
    reqs.emplace_back(std::move(e));
  }
  v["requests"] = std::move(reqs);
  auto lat = crow::json::wvalue::list();
  for (const auto& [route, h] : s.latency) {
    auto e = histogramJson(h);
    e["route"] = std::string{route};
    lat.emplace_back(std::move(e));
  }
  v["request_duration"] = std::move(lat);
  return v;
}

crow::json::wvalue libraryJson(const metrics::FullSnapshot& s) {
  crow::json::wvalue v;
  v["counters"] = crow::json::wvalue::object();
  v["gauges"] = crow::json::wvalue::object();
  v["histograms"] = crow::json::wvalue::object();
  for (const auto& c : s.counters) {
    v["counters"][c.name] = c.value;
  }
  for (const auto& g : s.gauges) {
    v["gauges"][g.name] = g.value;
  }
  for (const auto& h : s.histograms) {
    v["histograms"][h.name] = histogramJson(h);
  }
  return v;
}

std::string prometheusText(const ServerMetrics::Snapshot& s, const metrics::FullSnapshot& lib) {
  std::string out;
  out.reserve(16 * 1024);

  family(out, "phto_http_requests_total", "counter", "HTTP responses by method, route template and status class.");
  for (const auto& r : s.requests) {
    sample(
      out,
      "phto_http_requests_total",
      label("method", r.method) + "," + label("route", r.route) + "," + label("status_class", r.statusClass),
      std::to_string(r.count)
    );
  }
  family(out, "phto_http_request_duration_seconds", "histogram", "Request handling time by route template.");
  for (const auto& [route, h] : s.latency) {
    histogramSamples(out, "phto_http_request_duration_seconds", label("route", route), h);
  }
  family(out, "phto_http_requests_in_flight", "gauge", "Requests currently being handled.");
  sample(out, "phto_http_requests_in_flight", "", std::to_string(s.inFlight));
  family(out, "phto_http_rejected_total", "counter", "Requests answered 413, 401 or 429.");
  sample(out, "phto_http_rejected_total", label("reason", "too_large"), std::to_string(s.rejectedTooLarge));
  sample(out, "phto_http_rejected_total", label("reason", "unauthorized"), std::to_string(s.rejectedUnauthorized));
  sample(out, "phto_http_rejected_total", label("reason", "throttled"), std::to_string(s.rejectedThrottled));
  family(out, "phto_upload_bytes_total", "counter", "Bytes accepted into upload spools.");
  sample(out, "phto_upload_bytes_total", "", std::to_string(s.uploadBytes));
  family(out, "phto_logins_total", "counter", "POST /auth/login outcomes.");
  sample(out, "phto_logins_total", label("result", "success"), std::to_string(s.loginSuccess));
  sample(out, "phto_logins_total", label("result", "failure"), std::to_string(s.loginFailure));
  family(out, "phto_uptime_seconds", "gauge", "Seconds since server start.");
  sample(out, "phto_uptime_seconds", "", std::to_string(s.uptimeSeconds));
  family(out, "phto_process_resident_memory_bytes", "gauge", "Resident set size (VmRSS).");
  sample(out, "phto_process_resident_memory_bytes", "", std::to_string(s.rssBytes));
  family(out, "phto_process_resident_memory_peak_bytes", "gauge", "Peak resident set size (VmHWM).");
  sample(out, "phto_process_resident_memory_peak_bytes", "", std::to_string(s.peakRssBytes));

  for (const auto& c : lib.counters) {
    const std::string n = "phto_library_" + c.name + "_total";
    family(out, n, "counter", "Imager library counter " + c.name + ".");
    sample(out, n, "", std::to_string(c.value));
  }
  for (const auto& g : lib.gauges) {
    const std::string n = "phto_library_" + g.name;
    family(out, n, "gauge", "Imager library gauge " + g.name + ".");
    sample(out, n, "", std::to_string(g.value));
  }
  for (const auto& h : lib.histograms) {
    const std::string n = "phto_library_" + h.name + "_seconds";
    family(out, n, "histogram", "Imager library latency " + h.name + ".");
    histogramSamples(out, n, "", h);
  }
  return out;
}

} // namespace server
