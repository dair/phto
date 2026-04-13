#include "ProgressReporter.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "Output.h"
#include "TimeFormat.h"

namespace imagestore {

ProgressReporter::ProgressReporter(
  DisplayMode mode,
  const Stats& stats,
  SlotTracker& slots,
  ResultLog& resultLog,
  unsigned int jobs,
  bool dryRun,
  std::chrono::steady_clock::time_point startTime
)
  : m_mode(mode),
    m_stats(stats),
    m_slots(slots),
    m_resultLog(resultLog),
    m_jobs(jobs),
    m_dryRun(dryRun),
    m_startTime(startTime) {
  if (m_mode == DisplayMode::Verbose) {
    if (!isatty(STDERR_FILENO)) {
      std::cerr << "verbose: stderr is not a TTY, using line-based output\n";
      m_tty = false;
    } else {
      m_tty = true;

      // Query terminal dimensions
      struct winsize ws{};
      if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        m_termHeight = static_cast<int>(ws.ws_row);
        m_termWidth = static_cast<int>(ws.ws_col);
      }

      // Fixed section: min(jobs, 20) slot lines + separator + 2 stats + separator
      int maxSlotLines = std::min({static_cast<int>(m_jobs), 20, m_termHeight - 10});
      if (maxSlotLines < 1) {
        maxSlotLines = 1;
      }
      m_fixedHeight = maxSlotLines + 4;

      // Print blank lines to reserve space for the fixed section
      for (int i = 0; i < m_fixedHeight; ++i) {
        std::cerr << '\n';
      }

      // Set ANSI scrolling region to the bottom portion (below fixed section)
      std::cerr << "\033[" << (m_fixedHeight + 1) << ";" << m_termHeight << "r";

      // Hide cursor and move to top-left
      std::cerr << "\033[?25l\033[1;1H";
      std::cerr.flush();

      // Wire result log to write into the scrolling region
      m_resultLog.setTtyMode(true, m_fixedHeight + 1, m_termHeight);

      m_thread = std::thread(&ProgressReporter::loop, this);
    }
  }
  // Normal and Quiet modes: no background thread, no ANSI setup.
}

ProgressReporter::~ProgressReporter() {
  stop();
}

void ProgressReporter::stop() {
  if (m_stop.exchange(true)) {
    return; // already stopped
  }
  if (m_thread.joinable()) {
    m_thread.join();
  }
  if (m_mode == DisplayMode::Verbose && m_tty) {
    // Reset scrolling region, show cursor, position below fixed section
    std::lock_guard<std::mutex> lk(g_outputMutex);
    std::cerr << "\033[r"                                   // reset scrolling region
              << "\033[?25h"                                // show cursor
              << "\033[" << (m_fixedHeight + 1) << ";1H\n"; // move past fixed section
    std::cerr.flush();
  }
}

void ProgressReporter::printFinalSummary(double elapsedSeconds) const {
  if (m_mode == DisplayMode::Quiet) {
    return;
  }

  uint64_t processed = m_stats.processed.load();
  uint64_t added = m_stats.added.load();
  uint64_t duplicates = m_stats.duplicates.load();
  uint64_t errors = m_stats.errors.load();
  uint64_t skipped = m_stats.skipped.load();
  uint64_t totalBytes = m_stats.totalBytes.load();

  double filesPerSec = (elapsedSeconds > 0.0) ? static_cast<double>(processed) / elapsedSeconds : 0.0;
  double totalMB = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
  double totalGB = totalMB / 1024.0;

  std::lock_guard<std::mutex> lk(g_outputMutex);
  std::cerr << std::fixed;
  if (m_dryRun) {
    std::cerr << processed << " processed, " << added << " valid, " << duplicates << " would-be-duplicates, " << errors
              << " errors"
              << " — " << std::setprecision(1) << elapsedSeconds << "s (dry run, no writes)\n";
  } else {
    std::cerr << processed << " processed, " << added << " added, " << duplicates << " duplicates, " << errors
              << " errors, " << skipped << " skipped — " << std::setprecision(1) << elapsedSeconds << "s ("
              << std::setprecision(0) << filesPerSec << " files/s, ";
    if (totalGB >= 1.0) {
      std::cerr << std::setprecision(1) << totalGB << " GB total";
    } else {
      std::cerr << std::setprecision(1) << totalMB << " MB total";
    }
    std::cerr << ")\n";
  }
}

void ProgressReporter::loop() {
  using namespace std::chrono_literals;
  while (!m_stop.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(200ms);
    if (m_stop.load(std::memory_order_relaxed)) {
      break;
    }
    renderVerbose();
  }
}

void ProgressReporter::renderVerbose() {
  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count() / 1000.0;

  uint64_t processed = m_stats.processed.load(std::memory_order_relaxed);
  uint64_t added = m_stats.added.load(std::memory_order_relaxed);
  uint64_t duplicates = m_stats.duplicates.load(std::memory_order_relaxed);
  uint64_t errors = m_stats.errors.load(std::memory_order_relaxed);
  uint64_t skipped = m_stats.skipped.load(std::memory_order_relaxed);
  uint64_t totalBytes = m_stats.totalBytes.load(std::memory_order_relaxed);

  double filesPerSec = (elapsed > 0.0) ? static_cast<double>(processed) / elapsed : 0.0;
  double mbps = (elapsed > 0.0) ? (static_cast<double>(totalBytes) / (1024.0 * 1024.0)) / elapsed : 0.0;

  auto slots = m_slots.snapshot();
  int displaySlots = m_fixedHeight - 4; // total fixed height minus separators + stats lines

  std::ostringstream out;

  // Save cursor and move to top-left of fixed section
  out << "\0337\033[1;1H";

  // Slot lines
  for (int i = 0; i < displaySlots && i < static_cast<int>(slots.size()); ++i) {
    const auto& s = slots[static_cast<size_t>(i)];
    auto stageSecs = std::chrono::duration_cast<std::chrono::seconds>(now - s.stageStart).count();

    out << ' ';
    if (s.stage == PipelineStage::Idle) {
      out << std::left << std::setw(16) << "[idle]";
      fmtElapsed(out, stageSecs);
      out << "\033[K\n";
    } else {
      std::string label = std::string("[") + stageName(s.stage) + "]";
      out << std::left << std::setw(16) << label;
      fmtElapsed(out, stageSecs);
      out << "  ";

      // Truncate filename from the left to fit: leave 35 chars for prefix + time
      int maxNameLen = m_termWidth - 35;
      if (maxNameLen < 8) {
        maxNameLen = 8;
      }
      const std::string& name = s.filename;
      if (static_cast<int>(name.size()) > maxNameLen) {
        out << "..." << name.substr(name.size() - static_cast<size_t>(maxNameLen - 3));
      } else {
        out << name;
      }
      out << "\033[K\n";
    }
  }

  // Separator
  int sepWidth = std::min(m_termWidth - 2, 70);
  out << ' ' << std::string(static_cast<size_t>(sepWidth > 0 ? sepWidth : 40), '-') << "\033[K\n";

  // Stats line 1: counters
  out << std::fixed << std::setprecision(0);
  out << " Processed: " << processed << "  Written: " << added << "  Skipped: " << skipped << "  Errors: " << errors
      << "  DUP: " << duplicates << "\033[K\n";

  // Stats line 2: throughput
  out << " Throughput: " << std::setprecision(0) << filesPerSec << " files/s  " << std::setprecision(1) << mbps
      << " MB/s  Elapsed: " << elapsed << "s\033[K\n";

  // Separator
  out << ' ' << std::string(static_cast<size_t>(sepWidth > 0 ? sepWidth : 40), '-') << "\033[K\n";

  // Restore cursor to scrolling region
  out << "\0338";

  std::string output = out.str();
  std::lock_guard<std::mutex> lk(g_outputMutex);
  std::cerr << output;
  std::cerr.flush();
}

} // namespace imagestore
