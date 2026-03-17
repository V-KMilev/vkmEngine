#pragma once


#include <thread>

#include <deque>
#include <vector>

#include <mutex>
#include <atomic>
#include <condition_variable>

#include <type_traits>

#include "platform/threading/task.h"

namespace Engine {

const size_t DEFAULT_THREAD_COUNT = std::thread::hardware_concurrency();

class ThreadPool {
    public:
        ThreadPool(const ThreadPool& other) = delete;
        ThreadPool& operator=(const ThreadPool& other) = delete;

        ThreadPool(ThreadPool&& other) = delete;
        ThreadPool& operator=(ThreadPool&& other) = delete;

    public:
        static ThreadPool& get();

        size_t threadCount() const { return m_threads.size(); }
        size_t taskCount() const { return m_tasks.size(); }

        void addTask(Task && task);
        void addTasks(std::vector<Task>&& tasks);

        void waitToFinish();

    private:
        ThreadPool(size_t threadCount);
        ~ThreadPool();

        void start(size_t threadCount);
        void stop();

        void process();

    private:
        std::atomic<bool> m_running;
        std::atomic<size_t> m_taskCount;

        std::vector<std::thread> m_threads;
        std::deque<Task> m_tasks;

        std::mutex m_tasksMutex;
        std::condition_variable m_tasksCV;
};

template<class Function>
void parallelFor(size_t count, size_t grain, Function && function) {
    if (count == 0) {
        return;
    }

    grain = std::max(grain, size_t(1));

    auto invokeAt = [&](size_t i) {
        if constexpr (std::is_invocable_v<Function, size_t>) {
            function(i);
        } else {
            function();
        }
    };

    auto& pool = ThreadPool::get();

    // If there is enough work to justify threading overhead, submit the remaining chunks to the pool
    if (grain < count) {
        // Build all tasks, then submit in one batch (single lock + notify_all)
        std::vector<Task> tasks;
        for (size_t i = grain; i < count; i += grain) {
            size_t start = i;
            size_t end = std::min(i + grain, count);

            tasks.emplace_back(Task([start, end, &invokeAt]() {
                for (size_t i = start; i < end; ++i) {
                    invokeAt(i);
                }
            }));
        }
        pool.addTasks(std::move(tasks));
    }

    // In case of grain beeing bigger than count, we need to process the entire range on the main thread
    grain = std::min(grain, count);

    // Main thread processes first chunk instead of spinning idle in waitToFinish,
    // In case of grain beeing bigger than count, the main thread will process the entire range
    // This is to avoid the overhead of the threadpool for small ranges
    for (size_t i = 0; i < grain; ++i) {
        invokeAt(i);
    }

    pool.waitToFinish();
}

template<class Function>
void parallelFor(size_t count, Function && function) {
    auto& pool = ThreadPool::get();

    // The +1 is for the main thread
    size_t grain = count / (pool.threadCount() + 1);

    parallelFor(count, grain, function);
}

} // namespace Engine
