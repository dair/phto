#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace server {

/// In-memory, thread-safe brute-force throttle for POST /auth/login.
///
/// Tracks failure counts per key (a login name, or a client IP). Reaching `maxFailures` failures within
/// LOCKOUT_SECONDS of the first one locks the key out: all further attempts (including correct passwords)
/// are rejected with 429 until the lockout expires. Failures older than the window are forgotten.
/// recordSuccess() resets a key (used for per-login; the per-IP instance is never reset by a success, so
/// alternating a valid account with guesses against others cannot dodge the IP limit).
class LoginThrottle {
public:
  static constexpr int MAX_FAILURES = 5;      // default per-login threshold
  static constexpr int MAX_IP_FAILURES = 20;  // default per-IP threshold
  static constexpr int LOCKOUT_SECONDS = 900; // 15 minutes

  explicit LoginThrottle(int maxFailures = MAX_FAILURES)
    : m_maxFailures(maxFailures) {}

  /// Returns true if the key is currently locked out (too many recent failures).
  bool isLockedOut(const std::string& key);

  /// Record a failed attempt; increments failure counter for this key.
  void recordFailure(const std::string& key);

  /// Record a successful login; resets the failure counter for this key.
  void recordSuccess(const std::string& key);

private:
  using Clock = std::chrono::steady_clock;

  struct Entry {
    int failures{0};
    Clock::time_point windowStart{};
    Clock::time_point lockedUntil{};
  };

  /// Drops entries whose window and lockout have both expired (bounds memory under login rotation).
  void pruneLocked(Clock::time_point now);

  static constexpr size_t PRUNE_THRESHOLD = 4096;

  const int m_maxFailures;
  std::mutex m_mutex;
  std::unordered_map<std::string, Entry> m_entries;
};

} // namespace server
