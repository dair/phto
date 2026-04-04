#pragma once

#include <algorithm>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifdef IMAGER_METRICS_ENABLED
  #include <metrics/Metrics.h>

  #include <chrono>
#endif

namespace coro {

class ThreadPool {
public:
  explicit ThreadPool(size_t numThreads) {
    size_t n = std::max<size_t>(numThreads, 2u);
    m_workers.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      m_workers.emplace_back([this] { workerLoop(); });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock lock(m_mutex);
      m_stop = true;
    }
    m_cv.notify_all();
    // jthread joins automatically
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  /// Awaitable: co_await pool.schedule() resumes the coroutine on a worker thread.
  auto schedule() {
    struct Awaiter {
      ThreadPool& pool;
#ifdef IMAGER_METRICS_ENABLED
      std::chrono::steady_clock::time_point enqueueTime;
#endif
      bool await_ready() const noexcept {
        return false;
      }

      void await_suspend(std::coroutine_handle<> h) {
#ifdef IMAGER_METRICS_ENABLED
        enqueueTime = std::chrono::steady_clock::now();
        metrics::Metrics::get().pool_queue_depth.increment();
#endif
        pool.enqueue(h);
      }

      void await_resume() noexcept {
#ifdef IMAGER_METRICS_ENABLED
        metrics::Metrics::get().pool_queue_depth.decrement();
        auto elapsed = std::chrono::steady_clock::now() - enqueueTime;
        metrics::Metrics::get().pool_schedule_latency.record(elapsed);
#endif
      }
    };
#ifdef IMAGER_METRICS_ENABLED
    return Awaiter{*this, {}};
#else
    return Awaiter{*this};
#endif
  }

  void enqueue(std::coroutine_handle<> h) {
    {
      std::unique_lock lock(m_mutex);
      m_queue.push_back(h);
    }
    m_cv.notify_one();
  }

private:
  void workerLoop() {
    while (true) {
      std::coroutine_handle<> h;
      {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
        if (m_stop && m_queue.empty()) {
          return;
        }
        h = m_queue.front();
        m_queue.pop_front();
      }
#ifdef IMAGER_METRICS_ENABLED
      metrics::Metrics::get().pool_active_threads.increment();
#endif
      h.resume();
#ifdef IMAGER_METRICS_ENABLED
      metrics::Metrics::get().pool_active_threads.decrement();
#endif
    }
  }

  std::vector<std::jthread> m_workers;
  std::deque<std::coroutine_handle<>> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_stop{false};
};

} // namespace coro
