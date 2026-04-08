#pragma once

#include <iostream>
#include <mutex>
#include <string>

namespace imagestore {

inline std::mutex g_outputMutex;

inline void stderrLine(const std::string& line) {
  std::lock_guard<std::mutex> lk(g_outputMutex);
  std::cerr << line << '\n';
}

} // namespace imagestore
