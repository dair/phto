#include "ProgressReporter.h"

#include <metrics/Metrics.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iomanip>
#include <iostream>
#include <sstream>

#include "Output.h"

namespace imagestore {

namespace {

// ASCII bar character (full block via UTF-8 not needed; use '#' per plan Decision 5)
constexpr char BAR_CHAR = '#';

std::string renderBar(int64_t filled, int64_t total, int width) {
  int filledChars = (total > 0) ? static_cast<int>((filled * width) / total) : 0;
  if (filledChars > width) {
    filledChars = width;
  }
  if (filledChars < 0) {
    filledChars = 0;
  }
  std::string bar(static_cast<size_t>(filledChars), BAR_CHAR);
  bar += std::string(static_cast<size_t>(width - filledChars), ' ');
  return bar;
}

std::string fmtBytes(uint64_t bytes) {
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(1);
  double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
  double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
  if (gb >= 1.0) {
    ss << gb << " GB";
  } else {
    ss << mb << " MB";
  }
  return ss.str();
}

} // namespace

ProgressReporter::ProgressReporter(
  DisplayMode mode,
  const imager::Imager& img,
  const Stats& stats,
  unsigned int jobs,
  bool dryRun,
  std::chrono::steady_clock::time_point startTime
)
  : m_mode(mode),
    m_img(img),
    m_stats(stats),
    m_jobs(jobs),
    m_dryRun(dryRun),
    m_startTime(startTime) {
  if (m_mode == DisplayMode::Graph) {
    // TTY guard
    if (!isatty(STDERR_FILENO)) {
      std::cerr << "--graph: stderr is not a TTY, falling back to normal progress\n";
      m_mode = DisplayMode::Normal;
    } else {
      // Read terminal width
      struct winsize ws{};
      if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        m_termWidth = static_cast<int>(ws.ws_col);
      } else {
        m_termWidth = 80;
      }
      // bar width = terminal_width - 44, clamped to [10, 60]
      m_barWidth = m_termWidth - 44;
      if (m_barWidth < 10) {
        m_barWidth = 10;
      }
      if (m_barWidth > 60) {
        m_barWidth = 60;
      }

      // Hide cursor
      std::cerr << "\033[?25l";
    }
  }

  if (m_mode != DisplayMode::Quiet) {
    m_thread = std::thread(&ProgressReporter::loop, this);
  }
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
  if (m_mode == DisplayMode::Graph) {
    // Show cursor again and move past the graph block
    std::cerr << "\033[?25h\n";
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
  double totalGB = static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0);
  double totalMB = static_cast<double>(totalBytes) / (1024.0 * 1024.0);

  std::lock_guard<std::mutex> lk(g_outputMutex);
  std::cerr << std::fixed;
  if (m_dryRun) {
    std::cerr << processed << " processed, " << added << " valid, " << duplicates << " would-be-duplicates, " << errors
              << " errors"
              << " — " << std::setprecision(1) << elapsedSeconds << "s"
              << " (dry run, no writes)\n";
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
  auto interval = (m_mode == DisplayMode::Graph) ? 200ms : 1000ms;

  while (!m_stop.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(interval);
    if (m_stop.load(std::memory_order_relaxed)) {
      break;
    }

    if (m_mode == DisplayMode::Graph) {
      renderGraph();
    } else {
      renderNormal();
    }
  }
}

void ProgressReporter::renderNormal() const {
  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count() / 1000.0;

  uint64_t processed = m_stats.processed.load(std::memory_order_relaxed);
  uint64_t errors = m_stats.errors.load(std::memory_order_relaxed);
  uint64_t duplicates = m_stats.duplicates.load(std::memory_order_relaxed);
  uint64_t totalBytes = m_stats.totalBytes.load(std::memory_order_relaxed);

  double mbps = (elapsed > 0.0) ? (static_cast<double>(totalBytes) / (1024.0 * 1024.0)) / elapsed : 0.0;

  const auto& m = m_img.metrics();
  int64_t inFlight = m.inflight_validating.value() + m.inflight_hashing.value() + m.inflight_waiting_mutex.value() +
                     m.inflight_dedup_checking.value() + m.inflight_writing_storage.value() +
                     m.inflight_inserting_db.value() + m.inflight_reading.value();

  std::lock_guard<std::mutex> lk(g_outputMutex);
  std::cerr << "[progress] " << processed << " processed, " << errors << " errors, " << duplicates << " duplicates, "
            << std::fixed << std::setprecision(1) << mbps << " MB/s, " << inFlight << " in-flight\n";
}

void ProgressReporter::renderGraph() {
  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count() / 1000.0;

  uint64_t processed = m_stats.processed.load(std::memory_order_relaxed);
  uint64_t totalBytes = m_stats.totalBytes.load(std::memory_order_relaxed);

  double filesPerSec = (elapsed > 0.0) ? static_cast<double>(processed) / elapsed : 0.0;
  double gbps = (elapsed > 0.0) ? static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0) / elapsed : 0.0;
  double mbps = gbps * 1024.0;

  const auto& m = m_img.metrics();

  struct StageRow {
    const char* label;
    int64_t inflight;
    int64_t completed;
    int64_t bytesCompleted; // -1 = not shown
  };

  StageRow rows[] = {
    {"read",
     m.inflight_reading.value(),
     static_cast<int64_t>(m.stage_read.value()),
     static_cast<int64_t>(m.stage_read_bytes.value())},
    {"validate",
     m.inflight_validating.value(),
     static_cast<int64_t>(m.stage_validated.value()),
     static_cast<int64_t>(m.stage_validated_bytes.value())},
    {"hash",
     m.inflight_hashing.value(),
     static_cast<int64_t>(m.stage_hashed.value()),
     static_cast<int64_t>(m.stage_hashed_bytes.value())},
    {"mutex wait", m.inflight_waiting_mutex.value(), -1, -1},
    {"dedup check", m.inflight_dedup_checking.value(), static_cast<int64_t>(m.stage_dedup_checked.value()), -1},
    {"storage write",
     m.inflight_writing_storage.value(),
     static_cast<int64_t>(m.stage_stored.value()),
     static_cast<int64_t>(m.storage_bytes_written.value())},
    {"db insert",
     m.inflight_inserting_db.value(),
     static_cast<int64_t>(m.stage_db_inserted.value()),
     static_cast<int64_t>(m.stage_db_inserted_bytes.value())},
  };
  constexpr int NUM_ROWS = 7;
  // Total block lines: 1 header + 1 separator + 1 column header + NUM_ROWS stage rows + 1 separator + 1 pool line = 12
  constexpr int BLOCK_LINES = 12;

  std::ostringstream out;

  if (m_graphInitialized) {
    // Move cursor up to top of block and go to column 1
    out << "\033[" << BLOCK_LINES << "A\r";
  }

  // Line 1: header
  out << std::fixed << std::setprecision(1);
  out << "Elapsed: " << elapsed << "s   Files: " << std::setprecision(0) << filesPerSec << "/s   Throughput: ";
  if (gbps >= 1.0) {
    out << std::setprecision(2) << gbps << " GB/s";
  } else {
    out << std::setprecision(1) << mbps << " MB/s";
  }
  out << "\033[K\n";

  // Line 2: separator (ASCII dashes; Unicode horizontal line requires multibyte encoding)
  out << std::string(static_cast<size_t>(m_termWidth > 2 ? m_termWidth - 2 : 50), '-') << "\033[K\n";

  // Line 3: column header
  out << "Pipeline stages   [  in-flight / jobs  ]  completed   bytes\033[K\n";

  // Lines 4-10: stage rows
  for (int i = 0; i < NUM_ROWS; ++i) {
    const auto& r = rows[i];
    std::string bar = renderBar(r.inflight, static_cast<int64_t>(m_jobs), m_barWidth);
    out << "  " << std::left << std::setw(14) << r.label << " [" << bar << "] " << std::right << std::setw(3)
        << r.inflight << " /" << std::setw(3) << m_jobs;
    if (r.completed >= 0) {
      out << "  " << std::setw(8) << r.completed;
    } else {
      out << "          ";
    }
    if (r.bytesCompleted >= 0) {
      out << "  " << fmtBytes(static_cast<uint64_t>(r.bytesCompleted));
    }
    out << "\033[K\n";
  }

  // Line 11: separator
  out << std::string(static_cast<size_t>(m_termWidth > 2 ? m_termWidth - 2 : 50), '-') << "\033[K\n";

  // Line 12: thread pool
  out << "Thread pool: " << m.pool_active_threads.value() << " active  queue depth: " << m.pool_queue_depth.value()
      << "\033[K\n";

  std::string output = out.str();

  std::lock_guard<std::mutex> lk(g_outputMutex);
  std::cerr << output;

  m_graphInitialized = true;
}

} // namespace imagestore
