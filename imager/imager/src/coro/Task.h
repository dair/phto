#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace imager::coro {

// ---------------------------------------------------------------------------
// Task<T> — lazy, move-only coroutine return type
// ---------------------------------------------------------------------------

template <typename T>
class Task {
public:
    struct promise_type {
        std::optional<T>        value;
        std::exception_ptr      exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct Awaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                        std::coroutine_handle<promise_type> h) noexcept {
                    if (h.promise().continuation)
                        return h.promise().continuation;
                    return std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            return Awaiter{};
        }

        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    // Awaiter interface — called when someone co_awaits this Task
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        m_handle.promise().continuation = caller;
        return m_handle; // symmetric transfer: start the lazy coroutine
    }

    T await_resume() {
        if (m_handle.promise().exception)
            std::rethrow_exception(m_handle.promise().exception);
        return std::move(*m_handle.promise().value);
    }

    explicit Task(handle_type h) : m_handle(h) {}
    ~Task() { if (m_handle) m_handle.destroy(); }
    Task(Task&& o) noexcept : m_handle(std::exchange(o.m_handle, nullptr)) {}
    Task& operator=(Task&&) = delete;
    Task(const Task&)       = delete;
    Task& operator=(const Task&) = delete;

    /// Start the coroutine synchronously and return its result.
    /// Only valid if the coroutine has not been started yet (i.e., fresh Task).
    T runSync() {
        m_handle.resume();
        if (m_handle.promise().exception)
            std::rethrow_exception(m_handle.promise().exception);
        return std::move(*m_handle.promise().value);
    }

private:
    handle_type m_handle;
};

// ---------------------------------------------------------------------------
// Task<void> specialization
// ---------------------------------------------------------------------------

template <>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr      exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct Awaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                        std::coroutine_handle<promise_type> h) noexcept {
                    if (h.promise().continuation)
                        return h.promise().continuation;
                    return std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            return Awaiter{};
        }

        void return_void() {}
        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        m_handle.promise().continuation = caller;
        return m_handle;
    }

    void await_resume() {
        if (m_handle.promise().exception)
            std::rethrow_exception(m_handle.promise().exception);
    }

    explicit Task(handle_type h) : m_handle(h) {}
    ~Task() { if (m_handle) m_handle.destroy(); }
    Task(Task&& o) noexcept : m_handle(std::exchange(o.m_handle, nullptr)) {}
    Task& operator=(Task&&) = delete;
    Task(const Task&)       = delete;
    Task& operator=(const Task&) = delete;

    void runSync() {
        m_handle.resume();
        if (m_handle.promise().exception)
            std::rethrow_exception(m_handle.promise().exception);
    }

private:
    handle_type m_handle;
};

} // namespace imager::coro
