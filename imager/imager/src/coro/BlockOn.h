#pragma once

#include "Task.h"
#include "ThreadPool.h"

#include <exception>
#include <optional>
#include <semaphore>

namespace imager::coro {

// ---------------------------------------------------------------------------
// blockOn — run a coroutine to completion, blocking the calling thread.
//
// The coroutine (and its children) schedule themselves onto the pool via
// co_await pool.schedule(). blockOn just starts the chain and waits.
// ---------------------------------------------------------------------------

template <typename T>
T blockOn(ThreadPool& /*pool*/, Task<T> task) {
    std::binary_semaphore done{0};
    std::optional<T>      result;
    std::exception_ptr    error;

    // Wrapper coroutine: runs the inner task, then signals the semaphore.
    auto wrapper = [&]() -> Task<void> {
        try {
            result = co_await std::move(task);
        } catch (...) {
            error = std::current_exception();
        }
        done.release();
    };

    auto w = wrapper();
    // runSync() starts the wrapper. Via symmetric transfer it dives into the
    // inner task chain until the first real suspension (co_await pool.schedule()).
    // At that point resume() returns and pool threads take over.
    w.runSync();

    done.acquire();

    if (error) std::rethrow_exception(error);
    return std::move(*result);
}

inline void blockOn(ThreadPool& /*pool*/, Task<void> task) {
    std::binary_semaphore done{0};
    std::exception_ptr    error;

    auto wrapper = [&]() -> Task<void> {
        try {
            co_await std::move(task);
        } catch (...) {
            error = std::current_exception();
        }
        done.release();
    };

    auto w = wrapper();
    w.runSync();

    done.acquire();

    if (error) std::rethrow_exception(error);
}

} // namespace imager::coro
