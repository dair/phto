#include "ResultLog.h"

#include <iostream>

#include "Output.h"

namespace imagestore {

void ResultLog::setEnabled(bool enabled) {
  std::lock_guard<std::mutex> lk(m_mutex);
  m_enabled = enabled;
}

void ResultLog::setTtyMode(bool tty, int scrollTop, int scrollBottom) {
  std::lock_guard<std::mutex> lk(m_mutex);
  m_tty = tty;
  m_scrollTop = scrollTop;
  m_scrollBottom = scrollBottom;
}

void ResultLog::append(const std::string& line) {
  bool enabled;
  bool tty;
  int scrollBottom;
  {
    std::lock_guard<std::mutex> lk(m_mutex);
    enabled = m_enabled;
    tty = m_tty;
    scrollBottom = m_scrollBottom;
  }

  if (!enabled) {
    return;
  }

  if (tty) {
    // Write into the ANSI scrolling region.
    // The scrolling region (\033[top;bottomr) confines scrolling to the bottom
    // portion of the terminal. We position the cursor at the last row of the
    // scroll region, emit a newline to scroll the region up (creating a blank
    // line at the bottom), then print the result line there.
    std::lock_guard<std::mutex> lkOut(g_outputMutex);
    std::cerr << "\0337"                          // DEC save cursor
              << "\033[" << scrollBottom << ";1H" // move to last row of scroll region
              << "\r\n"                           // scroll region up, cursor at new bottom
              << line                             // print result
              << "\033[K"                         // clear to end of line
              << "\0338";                         // DEC restore cursor
    std::cerr.flush();
  } else {
    std::lock_guard<std::mutex> lkOut(g_outputMutex);
    std::cerr << line << '\n';
  }
}

} // namespace imagestore
