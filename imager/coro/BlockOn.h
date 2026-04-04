#pragma once

#include <exception>
#include <optional>
#include <semaphore>

#include "Task.h"
#include "ThreadPool.h"

namespace coro {

// ---------------------------------------------------------------------------
// blockOn — run a coroutine to completion, blocking the calling thread.
//
// The coroutine (and its children) schedule themselves onto the pool via
// co_await pool.schedule(). blockOn just starts the chain and waits.
// ---------------------------------------------------------------------------

// TerminalAwaiter signals 'sem' from within await_suspend.
//
// Why: when a co_await reaches its suspension point, the compiler writes the
// "suspended here" state into the coroutine frame BEFORE calling await_suspend.
// By releasing the semaphore from inside await_suspend, we guarantee that this
// write has happened-before the calling thread wakes up and destroys the frame.
// Releasing the semaphore from the coroutine body (after co_await task) would
// leave a window where the compiler-generated frame write for the subsequent
// final_suspend still races with the calling thread's frame destruction.
struct TerminalAwaiter {
  std::binary_semaphore& sem;

  bool await_ready() noexcept {
    return false;
  }

  void await_suspend(std::coroutine_handle<>) noexcept {
    sem.release();
  }

  void await_resume() noexcept {}
};

template<typename T>
T blockOn(ThreadPool& /*pool*/, Task<T> task) {
  std::binary_semaphore done{0};
  std::optional<T> result;
  std::exception_ptr error;

  // Wrapper coroutine: runs the inner task, then signals via TerminalAwaiter.
  auto wrapper = [&]() -> Task<void> {
    try {
      result = co_await std::move(task);
    } catch (...) {
      error = std::current_exception();
    }
    co_await TerminalAwaiter{done};
  };

  auto w = wrapper();
  // runSync() starts the wrapper. Via symmetric transfer it dives into the
  // inner task chain until the first real suspension (co_await pool.schedule()).
  // At that point resume() returns and pool threads take over.
  w.runSync();

  done.acquire();

  if (error) {
    std::rethrow_exception(error);
  }
  return std::move(*result);
}

inline void blockOn(ThreadPool& /*pool*/, Task<void> task) {
  std::binary_semaphore done{0};
  std::exception_ptr error;

  auto wrapper = [&]() -> Task<void> {
    try {
      co_await std::move(task);
    } catch (...) {
      error = std::current_exception();
    }
    co_await TerminalAwaiter{done};
  };

  auto w = wrapper();
  w.runSync();

  done.acquire();

  if (error) {
    std::rethrow_exception(error);
  }
}

} // namespace coro
