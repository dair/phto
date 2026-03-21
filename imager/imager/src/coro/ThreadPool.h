#pragma once

#include <algorithm>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace imager::coro {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) {
        size_t n = std::max<size_t>(numThreads, 2u);
        m_workers.reserve(n);
        for (size_t i = 0; i < n; ++i)
            m_workers.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool() {
        {
            std::unique_lock lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        // jthread joins automatically
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Awaitable: co_await pool.schedule() resumes the coroutine on a worker thread.
    auto schedule() {
        struct Awaiter {
            ThreadPool& pool;
            bool await_ready() const noexcept { return false; }
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
                if (m_stop && m_queue.empty()) return;
                h = m_queue.front();
                m_queue.pop_front();
            }
            h.resume();
        }
    }

    std::vector<std::jthread>           m_workers;
    std::deque<std::coroutine_handle<>> m_queue;
    std::mutex                          m_mutex;
    std::condition_variable             m_cv;
    bool                                m_stop{false};
};

} // namespace imager::coro
