#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace server {

/// In-memory, thread-safe brute-force throttle for POST /auth/login.
///
/// Tracks consecutive failure counts per key (login name). After MAX_FAILURES
/// consecutive failures within LOCKOUT_SECONDS, all further attempts (including
/// correct passwords) are rejected with 429 until the window expires.
/// A successful login resets the counter for that key.
class LoginThrottle {
public:
  static constexpr int MAX_FAILURES = 5;
  static constexpr int LOCKOUT_SECONDS = 900; // 15 minutes

  /// Returns true if the key is currently locked out (too many recent failures).
  bool isLockedOut(const std::string& key);

  /// Record a failed attempt; increments failure counter for this key.
  void recordFailure(const std::string& key);

  /// Record a successful login; resets the failure counter for this key.
  void recordSuccess(const std::string& key);

private:
  struct Entry {
    int failures{0};
    std::chrono::steady_clock::time_point lockedUntil{};
  };

  std::mutex m_mutex;
  std::unordered_map<std::string, Entry> m_entries;
};

} // namespace server
