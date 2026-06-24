#include "LoginThrottle.h"

namespace server {

bool LoginThrottle::isLockedOut(const std::string& key) {
  std::lock_guard<std::mutex> lock{m_mutex};
  auto it = m_entries.find(key);
  if (it == m_entries.end()) {
    return false;
  }
  const Entry& e = it->second;
  if (e.failures >= MAX_FAILURES) {
    const auto now = std::chrono::steady_clock::now();
    if (now < e.lockedUntil) {
      return true;
    }
    // Lockout window has expired — clear the entry.
    m_entries.erase(it);
  }
  return false;
}

void LoginThrottle::recordFailure(const std::string& key) {
  std::lock_guard<std::mutex> lock{m_mutex};
  Entry& e = m_entries[key];
  e.failures++;
  if (e.failures >= MAX_FAILURES) {
    e.lockedUntil = std::chrono::steady_clock::now() + std::chrono::seconds{LOCKOUT_SECONDS};
  }
}

void LoginThrottle::recordSuccess(const std::string& key) {
  std::lock_guard<std::mutex> lock{m_mutex};
  m_entries.erase(key);
}

} // namespace server
