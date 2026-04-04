#pragma once

#include "Histogram.h"

#include <chrono>

namespace metrics {

/// RAII timer: records elapsed duration into a Histogram on destruction.
///
/// Usage:
///   {
///       metrics::Timer t(metrics::Metrics::get().hash);
///       // ... work ...
///   }  // duration recorded here
class Timer {
public:
    explicit Timer(Histogram& target) noexcept;
    ~Timer();

    Timer(const Timer&)            = delete;
    Timer& operator=(const Timer&) = delete;

    /// Stop and record immediately. Subsequent calls to stop() or ~Timer() are no-ops.
    std::chrono::nanoseconds stop() noexcept;

private:
    std::chrono::nanoseconds record() noexcept;

    Histogram*                            m_target;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace metrics
