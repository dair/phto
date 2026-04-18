#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "DisplayMode.h"
#include "ResultLog.h"
#include "SlotTracker.h"
#include "Stats.h"

namespace imagestore {

class ProgressReporter {
public:
  ProgressReporter(
    DisplayMode mode,
    const Stats& stats,
    SlotTracker& slots,
    ResultLog& resultLog,
    unsigned int jobs,
    bool dryRun,
    std::chrono::steady_clock::time_point startTime,
    bool deleteMode = false
  );
  ~ProgressReporter();

  ProgressReporter(const ProgressReporter&) = delete;
  ProgressReporter& operator=(const ProgressReporter&) = delete;
  ProgressReporter(ProgressReporter&&) = delete;
  ProgressReporter& operator=(ProgressReporter&&) = delete;

  void stop();
  void printFinalSummary(double elapsedSeconds) const;

private:
  void loop();
  void renderVerbose();

  DisplayMode m_mode;
  const Stats& m_stats;
  SlotTracker& m_slots;
  ResultLog& m_resultLog;
  unsigned int m_jobs;
  bool m_dryRun;
  bool m_deleteMode;
  std::chrono::steady_clock::time_point m_startTime;
  bool m_tty{false};
  int m_termWidth{80};
  int m_termHeight{24};
  int m_fixedHeight{6};
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

} // namespace imagestore
