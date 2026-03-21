#pragma once

#include "Task.h"

#include <atomic>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace imager::coro {

// ---------------------------------------------------------------------------
// whenAll — start all tasks concurrently, wait for all to complete.
//
// Assumptions:
//   - Each task suspends at least once before completing (e.g. via
//     co_await pool.schedule()), so sub-tasks never complete synchronously
//     inside AllAwaiter::await_suspend. This prevents reentrancy issues.
//   - Tasks run on an external thread pool (captured by reference in lambdas).
// ---------------------------------------------------------------------------

namespace detail {

template <typename T>
struct WhenAllState {
    std::vector<Task<void>>         subTasks;   ///< Keeps handles alive until done
    std::vector<std::optional<T>>   results;
    std::vector<std::exception_ptr> exceptions;
    std::atomic<size_t>             remaining;
    std::coroutine_handle<>         continuation;

    explicit WhenAllState(size_t n)
        : results(n), exceptions(n), remaining(n) {}
};

template <>
struct WhenAllState<void> {
    std::vector<Task<void>>         subTasks;
    std::vector<std::exception_ptr> exceptions;
    std::atomic<size_t>             remaining;
    std::coroutine_handle<>         continuation;

    explicit WhenAllState(size_t n)
        : exceptions(n), remaining(n) {}
};

} // namespace detail

// ---------------------------------------------------------------------------
// whenAll<T>
// ---------------------------------------------------------------------------

template <typename T>
Task<std::vector<T>> whenAll(std::vector<Task<T>> tasks) {
    if (tasks.empty()) co_return {};

    const size_t n = tasks.size();
    auto state = std::make_shared<detail::WhenAllState<T>>(n);

    // Build sub-tasks that wrap each user task, store results, decrement counter.
    // Sub-tasks are stored in state->subTasks to keep handles alive until pool
    // threads finish running them.
    state->subTasks.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto subTask = [](std::shared_ptr<detail::WhenAllState<T>> s,
                          Task<T> innerTask, size_t idx) -> Task<void> {
            try {
                s->results[idx] = co_await std::move(innerTask);
            } catch (...) {
                s->exceptions[idx] = std::current_exception();
            }
            // Last to finish resumes the parent (whenAll coroutine).
            if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                s->continuation.resume();
            }
        }(state, std::move(tasks[i]), i);

        state->subTasks.push_back(std::move(subTask));
    }

    // Awaiter: stores the continuation handle and starts all sub-tasks.
    struct AllAwaiter {
        std::shared_ptr<detail::WhenAllState<T>> state;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            // Set continuation BEFORE starting sub-tasks to avoid races.
            state->continuation = h;
            for (auto& t : state->subTasks)
                t.runSync(); // starts lazy coroutine; suspends at co_await pool.schedule()
        }

        void await_resume() noexcept {}
    };

    co_await AllAwaiter{state};

    // Rethrow first exception, if any.
    for (auto& ep : state->exceptions)
        if (ep) std::rethrow_exception(ep);

    std::vector<T> out;
    out.reserve(n);
    for (auto& r : state->results)
        out.push_back(std::move(*r));
    co_return out;
}

// ---------------------------------------------------------------------------
// whenAll<void>
// ---------------------------------------------------------------------------

inline Task<void> whenAll(std::vector<Task<void>> tasks) {
    if (tasks.empty()) co_return;

    const size_t n = tasks.size();
    auto state = std::make_shared<detail::WhenAllState<void>>(n);

    state->subTasks.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto subTask = [](std::shared_ptr<detail::WhenAllState<void>> s,
                          Task<void> innerTask, size_t idx) -> Task<void> {
            try {
                co_await std::move(innerTask);
            } catch (...) {
                s->exceptions[idx] = std::current_exception();
            }
            if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                s->continuation.resume();
            }
        }(state, std::move(tasks[i]), i);

        state->subTasks.push_back(std::move(subTask));
    }

    struct AllAwaiter {
        std::shared_ptr<detail::WhenAllState<void>> state;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            state->continuation = h;
            for (auto& t : state->subTasks)
                t.runSync();
        }

        void await_resume() noexcept {}
    };

    co_await AllAwaiter{state};

    for (auto& ep : state->exceptions)
        if (ep) std::rethrow_exception(ep);
}

// ---------------------------------------------------------------------------
// whenAllSettled — like whenAll but NEVER throws; always waits for all tasks.
// Returns a vector of exception_ptr: nullptr = success, non-null = failure.
// Used for write-then-rollback patterns where partial failure must be handled.
// ---------------------------------------------------------------------------

namespace detail {

struct WhenAllSettledState {
    std::vector<Task<void>>         subTasks;
    std::vector<std::exception_ptr> exceptions;
    std::atomic<size_t>             remaining;
    std::coroutine_handle<>         continuation;

    explicit WhenAllSettledState(size_t n)
        : exceptions(n), remaining(n) {}
};

} // namespace detail

inline Task<std::vector<std::exception_ptr>>
whenAllSettled(std::vector<Task<void>> tasks) {
    if (tasks.empty()) co_return {};

    const size_t n = tasks.size();
    auto state = std::make_shared<detail::WhenAllSettledState>(n);

    state->subTasks.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto subTask = [](std::shared_ptr<detail::WhenAllSettledState> s,
                          Task<void> innerTask, size_t idx) -> Task<void> {
            // Always catch — never propagate exceptions out of sub-task
            try {
                co_await std::move(innerTask);
            } catch (...) {
                s->exceptions[idx] = std::current_exception();
            }
            if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                s->continuation.resume();
            }
        }(state, std::move(tasks[i]), i);

        state->subTasks.push_back(std::move(subTask));
    }

    struct SettledAwaiter {
        std::shared_ptr<detail::WhenAllSettledState> state;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            state->continuation = h;
            for (auto& t : state->subTasks)
                t.runSync();
        }

        void await_resume() noexcept {}
    };

    co_await SettledAwaiter{state};
    co_return state->exceptions; // move out — caller inspects each entry
}

} // namespace imager::coro
