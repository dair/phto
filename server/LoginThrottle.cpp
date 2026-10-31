#include "LoginThrottle.h"

namespace server {

bool LoginThrottle::isLockedOut(const std::string& key) {
  std::lock_guard<std::mutex> lock{m_mutex};
  auto it = m_entries.find(key);
  if (it == m_entries.end()) {
    return false;
  }
  const Entry& e = it->second;
  if (e.failures >= m_maxFailures) {
    if (Clock::now() < e.lockedUntil) {
      return true;
    }
    // Lockout window has expired — clear the entry.
    m_entries.erase(it);
  }
  return false;
}

void LoginThrottle::recordFailure(const std::string& key) {
  std::lock_guard<std::mutex> lock{m_mutex};
  const auto now = Clock::now();
  if (m_entries.size() >= PRUNE_THRESHOLD) {
    pruneLocked(now);
  }
  Entry& e = m_entries[key];
  if (e.failures > 0 && e.failures < m_maxFailures && now - e.windowStart > m_lockout) {
    e.failures = 0; // stale partial count: start a new window
  }
  if (e.failures == 0) {
    e.windowStart = now;
  }
  e.failures++;
  if (e.failures >= m_maxFailures) {
    e.lockedUntil = now + m_lockout;
  }
}

void LoginThrottle::recordSuccess(const std::string& key) {
  std::lock_guard<std::mutex> lock{m_mutex};
  m_entries.erase(key);
}

void LoginThrottle::pruneLocked(Clock::time_point now) {
  const auto window = m_lockout;
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    const Entry& e = it->second;
    const bool locked = e.failures >= m_maxFailures;
    if (locked ? now >= e.lockedUntil : now - e.windowStart > window) {
      it = m_entries.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace server
