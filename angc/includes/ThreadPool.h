#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <cassert>

namespace angara {

/// A simple fixed-size thread pool with a shared task queue.
///
/// Usage:
///   ThreadPool pool(4);
///   auto f1 = pool.enqueue([] { return 42; });
///   auto f2 = pool.enqueue([] { do_work(); });
///   int result = f1.get();  // blocks until done
///
/// The pool shuts down gracefully when destroyed: remaining tasks are
/// drained and workers are joined.  Tasks may be submitted from any
/// thread, including from within other tasks.
class ThreadPool {
public:
    /// Creates a thread pool with `num_threads` workers.
    /// If num_threads is 0, uses std::thread::hardware_concurrency().
    explicit ThreadPool(size_t num_threads = 0)
        : m_stop(false)
    {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 1;  // fallback
        }
        m_workers.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            m_workers.emplace_back([this] { worker_loop(); });
        }
    }

    /// Destroys the pool.  Sets the stop flag, wakes all workers, and joins
    /// them.  Any remaining queued tasks are NOT executed — they are dropped.
    /// Callers should ensure all futures have been retrieved before destroying
    /// the pool.
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /// Enqueues a task and returns a future for its result.
    /// The task is executed by one of the worker threads.
    /// Exceptions thrown by the task are propagated through the future.
    template <typename F>
    auto enqueue(F&& f) -> std::future<decltype(f())> {
        using return_type = decltype(f());

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(f));

        std::future<return_type> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();
        return result;
    }

    /// Returns the number of worker threads.
    size_t size() const { return m_workers.size(); }

    /// Returns the number of tasks currently queued (not yet started).
    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        return m_tasks.size();
    }

    // Non-copyable, non-movable.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_queue_mutex);
                m_condition.wait(lock, [this] {
                    return m_stop || !m_tasks.empty();
                });
                if (m_stop && m_tasks.empty()) {
                    return;
                }
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            task();
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    bool m_stop;
};

} // namespace angara
