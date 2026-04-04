#include <metrics/Gauge.h>

namespace metrics {

Gauge::Gauge(std::string name) : m_name(std::move(name)) {}

void Gauge::increment() noexcept {
    m_value.fetch_add(1, std::memory_order_relaxed);
}

void Gauge::decrement() noexcept {
    m_value.fetch_sub(1, std::memory_order_relaxed);
}

void Gauge::add(int64_t delta) noexcept {
    m_value.fetch_add(delta, std::memory_order_relaxed);
}

void Gauge::set(int64_t v) noexcept {
    m_value.store(v, std::memory_order_relaxed);
}

void Gauge::reset() noexcept {
    m_value.store(0, std::memory_order_relaxed);
}

int64_t Gauge::value() const noexcept {
    return m_value.load(std::memory_order_relaxed);
}

const std::string& Gauge::name() const noexcept {
    return m_name;
}

} // namespace metrics
