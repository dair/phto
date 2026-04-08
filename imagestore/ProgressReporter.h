#pragma once

#include <imager/Imager.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "DisplayMode.h"
#include "Stats.h"

namespace imagestore {

class ProgressReporter {
public:
  ProgressReporter(
    DisplayMode mode,
    const imager::Imager& img,
    const Stats& stats,
    unsigned int jobs,
    bool dryRun,
    std::chrono::steady_clock::time_point startTime
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
  void renderNormal() const;
  void renderGraph();

  DisplayMode m_mode;
  const imager::Imager& m_img;
  const Stats& m_stats;
  unsigned int m_jobs;
  bool m_dryRun;
  std::chrono::steady_clock::time_point m_startTime;
  bool m_graphInitialized{false};
  int m_barWidth{40};
  int m_termWidth{80};
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

} // namespace imagestore
