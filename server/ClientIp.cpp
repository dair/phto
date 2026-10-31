#include "ClientIp.h"

#include <arpa/inet.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <stdexcept>

namespace server {

namespace {

constexpr std::string_view WS = " \t";

std::string_view trim(std::string_view s) {
  const auto b = s.find_first_not_of(WS);
  if (b == std::string_view::npos) {
    return {};
  }
  return s.substr(b, s.find_last_not_of(WS) - b + 1);
}

/// Canonical text of an address (IPv4-mapped -> dotted quad), so equal clients share one throttle key.
std::string canonical(const std::array<uint8_t, 16>& a) {
  static constexpr std::array<uint8_t, 12> MAPPED{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
  char buf[INET6_ADDRSTRLEN] = {};
  if (std::equal(MAPPED.begin(), MAPPED.end(), a.begin())) {
    in_addr v4{};
    std::memcpy(&v4, a.data() + 12, 4);
    ::inet_ntop(AF_INET, &v4, buf, sizeof buf);
  } else {
    in6_addr v6{};
    std::memcpy(&v6, a.data(), 16);
    ::inet_ntop(AF_INET6, &v6, buf, sizeof buf);
  }
  return buf;
}

} // namespace

bool TrustedProxies::parseAddr(std::string_view text, Addr& out) {
  if (const auto pct = text.find('%'); pct != std::string_view::npos) {
    text = text.substr(0, pct); // drop an IPv6 zone id (fe80::1%eth0)
  }
  if (text.empty() || text.size() > 45) {
    return false;
  }
  const std::string z{text}; // inet_pton needs NUL termination
  in_addr v4{};
  if (::inet_pton(AF_INET, z.c_str(), &v4) == 1) {
    out = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    std::memcpy(out.data() + 12, &v4, 4);
    return true;
  }
  in6_addr v6{};
  if (::inet_pton(AF_INET6, z.c_str(), &v6) == 1) {
    std::memcpy(out.data(), &v6, 16);
    return true;
  }
  return false;
}

TrustedProxies TrustedProxies::parse(const std::vector<std::string>& entries) {
  TrustedProxies tp;
  for (const std::string& raw : entries) {
    const std::string_view e = trim(raw);
    const auto slash = e.find('/');
    Net n{};
    if (!parseAddr(e.substr(0, slash), n.addr) || e.substr(0, slash).find('%') != std::string_view::npos) {
      throw std::invalid_argument("trusted_proxies: invalid address or CIDR '" + raw + "'");
    }
    const bool v4 = e.substr(0, slash).find(':') == std::string_view::npos;
    n.bits = 128; // full match unless a prefix is given
    if (slash != std::string_view::npos) {
      const std::string_view p = e.substr(slash + 1);
      int len = -1;
      const auto [end, ec] = std::from_chars(p.data(), p.data() + p.size(), len);
      if (p.empty() || ec != std::errc{} || end != p.data() + p.size() || len < 0 || len > (v4 ? 32 : 128)) {
        throw std::invalid_argument("trusted_proxies: invalid prefix length in '" + raw + "'");
      }
      n.bits = v4 ? 96 + len : len;
    }
    // Zero the host bits so entries compare as networks ("10.1.2.3/8" == "10.0.0.0/8").
    for (int i = 0; i < 16; ++i) {
      const int keep = std::clamp(n.bits - i * 8, 0, 8);
      n.addr[static_cast<size_t>(i)] &= static_cast<uint8_t>(0xff00u >> keep);
    }
    tp.m_nets.push_back(n);
  }
  return tp;
}

bool TrustedProxies::contains(const Addr& a) const {
  return std::ranges::any_of(m_nets, [&](const Net& n) {
    for (int i = 0; i < 16; ++i) {
      const int keep = std::clamp(n.bits - i * 8, 0, 8);
      const auto mask = static_cast<uint8_t>(0xff00u >> keep);
      if ((a[static_cast<size_t>(i)] & mask) != n.addr[static_cast<size_t>(i)]) {
        return false;
      }
    }
    return true;
  });
}

bool TrustedProxies::contains(std::string_view ip) const {
  Addr a{};
  return parseAddr(trim(ip), a) && contains(a);
}

std::string TrustedProxies::clientIp(std::string_view peer, std::string_view xForwardedFor) const {
  std::string fallback{peer};
  Addr peerAddr{};
  if (m_nets.empty() || !parseAddr(peer, peerAddr) || !contains(peerAddr)) {
    return fallback;
  }
  std::string_view rest = xForwardedFor;
  while (true) {
    const auto comma = rest.rfind(',');
    const std::string_view tok = trim(comma == std::string_view::npos ? rest : rest.substr(comma + 1));
    Addr a{};
    if (!parseAddr(tok, a)) {
      return fallback; // empty or malformed entry: do not guess
    }
    if (!contains(a)) {
      return canonical(a);
    }
    if (comma == std::string_view::npos) {
      return fallback; // every hop was a trusted proxy
    }
    rest = rest.substr(0, comma);
  }
}

} // namespace server
