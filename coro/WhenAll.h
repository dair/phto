#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

#include "Task.h"

namespace coro {

// ---------------------------------------------------------------------------
// whenAll — start all tasks concurrently, wait for all to complete.
//
// Assumptions:
//   - INVARIANT: All tasks passed to whenAll() MUST suspend at least once
//     (e.g. via co_await pool.schedule()) before producing a result.
//     If a sub-task completes synchronously, the last-to-finish decrement
//     may resume the continuation from within await_suspend, which is
//     undefined behaviour in the C++20 coroutine model.
//     Callers: ensure every whenAll task begins with co_await pool.schedule()
//     or equivalent before doing any work.
//   - Tasks run on an external thread pool (captured by reference in lambdas).
// ---------------------------------------------------------------------------

namespace detail {

template<typename T>
struct WhenAllState {
  // subTasks removed: sub-tasks now self-destroy via SubTask coroutine type,
  // so no RAII wrapper is needed here. The state shared_ptr is kept alive
  // by each sub-task's captured copy until all sub-tasks complete and
  // self-destruct, breaking the reference cycle that previously caused leaks.
  std::vector<std::optional<T>> results;
  std::vector<std::exception_ptr> exceptions;
  std::atomic<size_t> remaining;
  std::coroutine_handle<> continuation;

  explicit WhenAllState(size_t n)
    : results(n),
      exceptions(n),
      remaining(n) {}
};

template<>
struct WhenAllState<void> {
  // subTasks removed — same rationale as WhenAllState<T>.
  std::vector<std::exception_ptr> exceptions;
  std::atomic<size_t> remaining;
  std::coroutine_handle<> continuation;

  explicit WhenAllState(size_t n)
    : exceptions(n),
      remaining(n) {}
};

// SubTask — a self-destroying coroutine type used as the wrapper inside
// whenAll/whenAllSettled. Using final_suspend = suspend_never causes the
// compiler to automatically destroy the coroutine frame when it finishes,
// without any external RAII owner. This avoids the shared_ptr cycle that
// would arise if WhenAllState held Task<void> wrappers that in turn captured
// the state shared_ptr.
struct SubTask {
  struct promise_type {
    SubTask get_return_object() noexcept {
      return SubTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    // final_suspend = suspend_never: frame auto-destroys when the coroutine
    // body finishes. Safe because no external code holds or destroys the handle.
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {} // exceptions handled inside body
  };

  std::coroutine_handle<promise_type> handle;

  explicit SubTask(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}

  // Non-copyable, non-movable — started immediately, then forgotten.
  SubTask(const SubTask&) = delete;
  SubTask& operator=(const SubTask&) = delete;
  SubTask(SubTask&&) = delete;
  SubTask& operator=(SubTask&&) = delete;

  // Start the coroutine and release ownership: the coroutine is self-destroying
  // (final_suspend = suspend_never), so no destructor call is needed here.
  void start() noexcept {
    handle.resume();
    // After resume() the coroutine either suspended (at co_await pool.schedule())
    // or completed and self-destroyed. Either way, we must not touch handle again.
  }
};

struct WhenAllSettledState {
  // subTasks removed — same rationale as WhenAllState<T>.
  std::vector<std::exception_ptr> exceptions;
  std::atomic<size_t> remaining;
  std::coroutine_handle<> continuation;

  explicit WhenAllSettledState(size_t n)
    : exceptions(n),
      remaining(n) {}
};

} // namespace detail

// ---------------------------------------------------------------------------
// whenAll<T>
// ---------------------------------------------------------------------------

template<typename T>
Task<std::vector<T>> whenAll(std::vector<Task<T>> tasks) {
  if (tasks.empty()) {
    co_return {};
  }

  const size_t n = tasks.size();
  auto state = std::make_shared<detail::WhenAllState<T>>(n);

  // Awaiter: stores the continuation handle and starts all sub-tasks.
  struct AllAwaiter {
    std::shared_ptr<detail::WhenAllState<T>> state;
    std::vector<Task<T>> tasks;

    bool await_ready() const noexcept {
      return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      // Set continuation BEFORE starting sub-tasks to avoid races.
      state->continuation = h;
      const size_t count = tasks.size();
      for (size_t i = 0; i < count; ++i) {
        // Build and immediately start a self-destroying sub-task wrapper.
        // The sub-task captures state and innerTask by value; state keeps
        // results/exceptions alive, and the sub-task self-destructs at
        // completion (final_suspend = suspend_never), releasing both captures.
        auto subTask = [](std::shared_ptr<detail::WhenAllState<T>> s, Task<T> innerTask, size_t idx) -> detail::SubTask {
          try {
            s->results[idx] = co_await std::move(innerTask);
          } catch (...) {
            s->exceptions[idx] = std::current_exception();
          }
          // Last to finish resumes the parent (whenAll coroutine).
          if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            s->continuation.resume();
          }
          // SubTask frame auto-destroys here (final_suspend = suspend_never),
          // releasing the `s` and `innerTask` captures.
        }(state, std::move(tasks[i]), i);
        subTask.start();
        // subTask goes out of scope here; handle is not owned (start() released it).
      }
    }

    void await_resume() noexcept {}
  };

  co_await AllAwaiter{state, std::move(tasks)};

  // Rethrow first exception, if any.
  for (auto& ep : state->exceptions) {
    if (ep) {
      std::rethrow_exception(ep);
    }
  }

  std::vector<T> out;
  out.reserve(n);
  for (auto& r : state->results) {
    out.push_back(std::move(*r));
  }
  co_return out;
}

// ---------------------------------------------------------------------------
// whenAll<void>
// ---------------------------------------------------------------------------

inline Task<void> whenAll(std::vector<Task<void>> tasks) {
  if (tasks.empty()) {
    co_return;
  }

  const size_t n = tasks.size();
  auto state = std::make_shared<detail::WhenAllState<void>>(n);

  struct AllAwaiter {
    std::shared_ptr<detail::WhenAllState<void>> state;
    std::vector<Task<void>> tasks;

    bool await_ready() const noexcept {
      return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      state->continuation = h;
      const size_t count = tasks.size();
      for (size_t i = 0; i < count; ++i) {
        auto subTask = [](std::shared_ptr<detail::WhenAllState<void>> s, Task<void> innerTask, size_t idx) -> detail::SubTask {
          try {
            co_await std::move(innerTask);
          } catch (...) {
            s->exceptions[idx] = std::current_exception();
          }
          if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            s->continuation.resume();
          }
          // SubTask frame auto-destroys here, releasing `s` and `innerTask`.
        }(state, std::move(tasks[i]), i);
        subTask.start();
      }
    }

    void await_resume() noexcept {}
  };

  co_await AllAwaiter{state, std::move(tasks)};

  for (auto& ep : state->exceptions) {
    if (ep) {
      std::rethrow_exception(ep);
    }
  }
}

// ---------------------------------------------------------------------------
// whenAllSettled — like whenAll but NEVER throws; always waits for all tasks.
// Returns a vector of exception_ptr: nullptr = success, non-null = failure.
// Used for write-then-rollback patterns where partial failure must be handled.
// ---------------------------------------------------------------------------

inline Task<std::vector<std::exception_ptr>> whenAllSettled(std::vector<Task<void>> tasks) {
  if (tasks.empty()) {
    co_return {};
  }

  const size_t n = tasks.size();
  auto state = std::make_shared<detail::WhenAllSettledState>(n);

  struct SettledAwaiter {
    std::shared_ptr<detail::WhenAllSettledState> state;
    std::vector<Task<void>> tasks;

    bool await_ready() const noexcept {
      return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      state->continuation = h;
      const size_t count = tasks.size();
      for (size_t i = 0; i < count; ++i) {
        auto subTask = [](std::shared_ptr<detail::WhenAllSettledState> s, Task<void> innerTask, size_t idx) -> detail::SubTask {
          // Always catch — never propagate exceptions out of sub-task.
          try {
            co_await std::move(innerTask);
          } catch (...) {
            s->exceptions[idx] = std::current_exception();
          }
          if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            s->continuation.resume();
          }
          // SubTask frame auto-destroys here, releasing `s` and `innerTask`.
        }(state, std::move(tasks[i]), i);
        subTask.start();
      }
    }

    void await_resume() noexcept {}
  };

  co_await SettledAwaiter{state, std::move(tasks)};
  co_return state->exceptions; // move out — caller inspects each entry
}

} // namespace coro
