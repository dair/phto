#include <metrics/Counter.h>

namespace metrics {

Counter::Counter(std::string name) : m_name(std::move(name)) {}

void Counter::add(uint64_t delta) noexcept {
    m_value.fetch_add(delta, std::memory_order_relaxed);
}

void Counter::reset() noexcept {
    m_value.store(0, std::memory_order_relaxed);
}

uint64_t Counter::value() const noexcept {
    return m_value.load(std::memory_order_relaxed);
}

const std::string& Counter::name() const noexcept {
    return m_name;
}

} // namespace metrics
