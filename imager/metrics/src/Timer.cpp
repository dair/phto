#include <metrics/Timer.h>

namespace metrics {

Timer::Timer(Histogram& target) noexcept
    : m_target(&target)
    , m_start(std::chrono::steady_clock::now()) {}

Timer::~Timer() {
    if (m_target) record();
}

std::chrono::nanoseconds Timer::stop() noexcept {
    if (!m_target) return std::chrono::nanoseconds{0};
    auto elapsed = record();
    m_target = nullptr;
    return elapsed;
}

std::chrono::nanoseconds Timer::record() noexcept {
    auto elapsed = std::chrono::steady_clock::now() - m_start;
    m_target->record(elapsed);
    return elapsed;
}

} // namespace metrics
