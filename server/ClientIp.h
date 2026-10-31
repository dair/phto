#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace server {

/// Set of trusted reverse proxies (IPv4/IPv6 addresses or CIDRs) and the client-IP resolution that depends on it.
class TrustedProxies {
public:
  /// Parses "10.0.0.1", "10.0.0.0/8", "::1", "fd00::/8", ... Throws std::invalid_argument naming the bad entry.
  static TrustedProxies parse(const std::vector<std::string>& entries);

  bool empty() const noexcept {
    return m_nets.empty();
  }

  /// True if `ip` (textual IPv4/IPv6; an IPv4-mapped IPv6 form matches IPv4 entries) is in the set.
  bool contains(std::string_view ip) const;

  /// The address to attribute a request to. Without trusted proxies, or when `peer` is not one, this is `peer`
  /// and `xForwardedFor` is ignored. Otherwise the X-Forwarded-For chain is walked right to left, skipping
  /// trusted entries; the first untrusted entry is the client. A malformed or empty entry, or a chain made only
  /// of trusted entries, falls back to `peer`.
  std::string clientIp(std::string_view peer, std::string_view xForwardedFor) const;

private:
  using Addr = std::array<uint8_t, 16>; // IPv4 stored as IPv4-mapped IPv6 (::ffff:a.b.c.d)

  struct Net {
    Addr addr;
    int bits; // prefix length in the 128-bit form
  };

  static bool parseAddr(std::string_view text, Addr& out);
  bool contains(const Addr& a) const;

  std::vector<Net> m_nets;
};

} // namespace server
