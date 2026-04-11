#pragma once

#include <metrics/Metrics.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace coro {

class ThreadPool {
  struct QueueEntry {
    std::coroutine_handle<> handle;
    std::chrono::steady_clock::time_point enqueued;
  };

public:
  explicit ThreadPool(size_t numThreads, metrics::Metrics* m = nullptr)
    : m_metrics(m) {
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

      bool await_ready() const noexcept {
        return false;
      }

      void await_suspend(std::coroutine_handle<> h) {
        pool.enqueue(h);
      }

      void await_resume() noexcept {}
    };

    return Awaiter{*this};
  }

  void enqueue(std::coroutine_handle<> h) {
    {
      std::unique_lock lock(m_mutex);
      m_queue.push_back({h, std::chrono::steady_clock::now()});
      if (m_metrics) {
        m_metrics->pool_queue_depth.increment();
      }
    }
    m_cv.notify_one();
  }

private:
  void workerLoop() {
    while (true) {
      QueueEntry entry{};
      {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
        if (m_stop && m_queue.empty()) {
          return;
        }
        entry = m_queue.front();
        m_queue.pop_front();
        if (m_metrics) {
          m_metrics->pool_queue_depth.decrement();
        }
      }
      if (m_metrics) {
        auto latency = std::chrono::steady_clock::now() - entry.enqueued;
        m_metrics->pool_schedule_latency.record(latency);
        m_metrics->pool_active_threads.increment();
      }
      entry.handle.resume();
      if (m_metrics) {
        m_metrics->pool_active_threads.decrement();
      }
    }
  }

  std::vector<std::jthread> m_workers;
  std::deque<QueueEntry> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_stop{false};
  metrics::Metrics* m_metrics{nullptr};
};

} // namespace coro
