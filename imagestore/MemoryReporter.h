#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include <metrics/Metrics.h>

#include "ResultLog.h"
#include "Stats.h"

namespace imagestore {

class MemoryReporter {
public:
  MemoryReporter(
    bool enabled,
    std::chrono::seconds interval,
    const Stats& stats,
    const metrics::Metrics& metrics,
    ResultLog& resultLog
  );
  ~MemoryReporter();

  MemoryReporter(const MemoryReporter&) = delete;
  MemoryReporter& operator=(const MemoryReporter&) = delete;
  MemoryReporter(MemoryReporter&&) = delete;
  MemoryReporter& operator=(MemoryReporter&&) = delete;

  void stop();

private:
  void loop();
  void emitSample(bool finalSample) const;

  bool m_enabled{false};
  std::chrono::seconds m_interval{5};
  const Stats& m_stats;
  const metrics::Metrics& m_metrics;
  ResultLog& m_resultLog;
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

} // namespace imagestore
