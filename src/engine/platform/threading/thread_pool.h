#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace Engine {

/**
 * @brief Fixed-size pool of worker threads draining a shared task queue.
 *
 * Process-wide singleton (get()); the constructor/destructor are private.
 * Used to back parallelFor; workers dequeue tasks in FIFO submission order
 * but execute them concurrently (no ordering of completion).
 * waitToFinish() blocks until every submitted task has completed.
 */
class ThreadPool {
    public:
        ThreadPool(const ThreadPool& other) = delete;
        ThreadPool& operator=(const ThreadPool& other) = delete;

        ThreadPool(ThreadPool && other) = delete;
        ThreadPool& operator=(ThreadPool && other) = delete;

    public:
        /** @brief Access the process-wide pool, constructed on first use. */
        static ThreadPool& get();

        size_t threadCount() const { return m_threads.size(); }

        /** @brief Enqueue a single task and wake one worker. */
        void addTask(std::function<void()> && task);

        /**
         * @brief Enqueue a batch of tasks under one lock and wake all workers.
         * Cheaper than repeated addTask for many tasks at once.
         */
        void addTasks(std::vector<std::function<void()>> && tasks);

        /**
         * @brief Block the caller until the in-flight task count reaches zero.
         * Counts every task ever submitted (not a per-call barrier), so do
         * not interleave unrelated submissions across waitToFinish() calls.
         */
        void waitToFinish();

        /**
         * @brief True when called from a thread owned by the pool. parallelFor
         * must not be re-entered from inside a task body - the calling
         * worker would spin in waitToFinish() forever on its own slot.
         */
        static bool isWorkerThread();

    private:
        ThreadPool(size_t threadCount);
        ~ThreadPool();

        /** @brief Spawn the worker threads. */
        void start(size_t threadCount);
        /** @brief Signal shutdown and join all workers; clears any unrun tasks. */
        void stop();

        /**
         * @brief Worker loop: pop and run tasks until shutdown, decrementing the
         * in-flight count (even on exception) and signalling waiters at zero.
         */
        void process();

    private:
        std::atomic<bool> m_running;
        std::atomic<size_t> m_taskCount;

        std::vector<std::thread> m_threads;
        std::deque<std::function<void()>> m_tasks;

        std::mutex m_tasksMutex;
        std::condition_variable m_tasksCV;
        std::condition_variable m_doneCV;   ///< Signalled when m_taskCount drops to 0
};

/**
 * @brief Run @p function over the index range [0, count) split into @p grain-sized
 * chunks across the pool, with the calling thread handling the first chunk.
 * @p function may take a size_t index or no arguments. Re-entry from a worker
 * thread falls back to a serial sweep to avoid self-deadlock. Blocks until the
 * whole range is done.
 */
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

    // Re-entering parallelFor from inside a worker would deadlock: the
    // worker's own slot stays in m_taskCount, so waitToFinish() never
    // returns. Fall back to a serial sweep on the calling worker thread.
    if (ThreadPool::isWorkerThread()) {
        for (size_t i = 0; i < count; ++i) invokeAt(i);
        return;
    }

    auto& pool = ThreadPool::get();

    // If there is enough work to justify threading overhead, submit the remaining chunks to the pool
    if (grain < count) {
        // Build all tasks, then submit in one batch (single lock + notify_all)
        std::vector<std::function<void()>> tasks;
        for (size_t i = grain; i < count; i += grain) {
            size_t start = i;
            size_t end = std::min(i + grain, count);

            tasks.emplace_back([start, end, &invokeAt]() {
                for (size_t i = start; i < end; ++i) {
                    invokeAt(i);
                }
            });
        }
        pool.addTasks(std::move(tasks));
    }

    // In case of grain being bigger than count, we need to process the entire range on the main thread
    grain = std::min(grain, count);

    // Main thread processes first chunk instead of spinning idle in waitToFinish,
    // In case of grain being bigger than count, the main thread will process the entire range
    // This is to avoid the overhead of the threadpool for small ranges
    for (size_t i = 0; i < grain; ++i) {
        invokeAt(i);
    }

    pool.waitToFinish();
}

/**
 * @brief parallelFor with an auto-chosen grain: ranges below MIN_PARALLEL run inline
 * to dodge dispatch overhead; larger ranges are split evenly across all
 * workers plus the calling thread.
 */
template<class Function>
void parallelFor(size_t count, Function && function) {
    auto& pool = ThreadPool::get();

    // Below this many items the pool's dispatch cost (mutex + notify_all wake
    // of every worker + done-CV round trip) dwarfs the per-item work, so run
    // the range inline. grain == count makes the call below submit zero tasks
    // and sweep serially on the calling thread - no parallelism tax for the
    // small workloads that dominate typical scenes.
    constexpr size_t MIN_PARALLEL = 2048;

    // The +1 is for the main thread.
    const size_t grain = (count < MIN_PARALLEL)
        ? count
        : count / (pool.threadCount() + 1);

    parallelFor(count, grain, function);
}

} // namespace Engine
