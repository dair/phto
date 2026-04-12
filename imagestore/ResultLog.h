#pragma once

#include <mutex>
#include <string>

namespace imagestore {

/// Thread-safe result line logger.
/// In TTY verbose mode, writes into the ANSI scrolling region.
/// In non-TTY mode, writes directly to stderr.
class ResultLog {
public:
  ResultLog() = default;

  ResultLog(const ResultLog&) = delete;
  ResultLog& operator=(const ResultLog&) = delete;
  ResultLog(ResultLog&&) = delete;
  ResultLog& operator=(ResultLog&&) = delete;

  /// Append a result line. Thread-safe.
  void append(const std::string& line);

  /// Configure TTY mode. When tty=true, result lines are written using
  /// ANSI escape sequences into the scrolling region bounded by
  /// [scrollTop, scrollBottom]. Call once at startup before workers start.
  void setTtyMode(bool tty, int scrollTop = 0, int scrollBottom = 0);

  /// Disable all output (quiet mode).
  void setEnabled(bool enabled);

private:
  std::mutex m_mutex;
  bool m_enabled{true};
  bool m_tty{false};
  int m_scrollTop{0};
  int m_scrollBottom{0};
};

} // namespace imagestore
