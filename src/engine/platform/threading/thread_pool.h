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
 * but execute them concurrently (no ordering of completion). Waiting is
 * per-batch (addTasks + waitForBatch), so a caller never blocks on work
 * someone else queued - the async asset decodes share this pool.
 */
class ThreadPool {
    public:
        ThreadPool(const ThreadPool& other) = delete;
        ThreadPool& operator=(const ThreadPool& other) = delete;

        ThreadPool(ThreadPool && other) = delete;
        ThreadPool& operator=(ThreadPool && other) = delete;

    public:
        /**
         * @brief Access the process-wide thread pool.
         *
         * The pool is constructed on first use (Meyers singleton) and shared by
         * every caller for the lifetime of the process.
         *
         * @return Reference to the single process-wide pool.
         */
        static ThreadPool& get();

        size_t threadCount() const { return m_threads.size(); }

        /**
         * @brief Enqueue a single task and wake one worker.
         *
         * @param task The work to run on a worker thread; consumed (moved into
         *             the queue).
         */
        void addTask(std::function<void()> && task);

        /**
         * @brief Enqueue a batch of tasks under one lock and wake all workers.
         *
         * Cheaper than repeated addTask for many tasks at once. @p pending is
         * raised by the batch size here and dropped as each of its tasks
         * retires - even one that throws - so waitForBatch() blocks on this
         * batch alone and not on whatever else is in the queue.
         *
         * @param tasks The work to run on worker threads; consumed (moved into
         *              the queue).
         * @param pending The caller's completion counter for this batch; it
         *                must outlive the tasks, which the matching
         *                waitForBatch() guarantees.
         */
        void addTasks(std::vector<std::function<void()>> && tasks, std::atomic<size_t>& pending);

        /**
         * @brief Block the caller until every task of one batch has retired.
         *
         * Returns immediately when the batch is already done (or was never
         * submitted), so the serial path of parallelFor pays nothing.
         *
         * @param pending The counter handed to addTasks for that batch.
         */
        void waitForBatch(std::atomic<size_t>& pending);

        /**
         * @brief Join the workers now, ahead of the pool's own destruction.
         *
         * Tasks already started run to completion; queued ones are dropped.
         * Call this while the objects those tasks write into are still alive:
         * the pool is a function-local static, so its destructor runs after
         * other singletons' (reverse construction order) and a decode landing
         * then would push into a destroyed queue. Idempotent; once the workers
         * are gone parallelFor sweeps serially.
         */
        void shutdown();

        /**
         * @brief True when called from a thread owned by the pool. parallelFor
         * must not be re-entered from inside a task body - with every worker
         * blocked on chunks queued behind them, nothing is left to run them.
         */
        static bool isWorkerThread();

    private:
        /**
         * @brief One queued task plus the batch counter it retires against
         *        (null for a fire-and-forget addTask).
         */
        struct QueuedTask {
            std::function<void()> function;
            std::atomic<size_t>*  pending = nullptr;
        };

    private:
        ThreadPool(size_t threadCount);
        ~ThreadPool();

        /**
         * @brief Worker loop: pop and run tasks until shutdown, retiring each
         * against its batch counter (even on exception) and signalling
         * waiters when a batch reaches zero.
         */
        void process();

    private:
        std::atomic<bool> m_running;

        std::vector<std::thread> m_threads;
        std::deque<QueuedTask> m_tasks;

        std::mutex m_tasksMutex;
        std::condition_variable m_tasksCV;
        std::condition_variable m_doneCV;   ///< Signalled when a batch counter drops to 0
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

    // Re-entering parallelFor from inside a worker risks deadlock: every worker
    // could end up waiting on chunks queued behind the workers themselves. Fall
    // back to a serial sweep on the calling worker thread.
    if (ThreadPool::isWorkerThread()) {
        for (size_t i = 0; i < count; ++i) invokeAt(i);
        return;
    }

    auto& pool = ThreadPool::get();

    // Nobody to hand chunks to once the pool has been shut down (or on a
    // platform that reported no cores) - a submission there would never retire.
    if (pool.threadCount() == 0) {
        for (size_t i = 0; i < count; ++i) invokeAt(i);
        return;
    }

    // This call's own completion count: the pool is shared with the async asset
    // decodes, so waiting on anything global would park the caller behind an
    // unrelated texture read.
    std::atomic<size_t> pending{0};

    if (grain < count) {
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
        pool.addTasks(std::move(tasks), pending);
    }

    grain = std::min(grain, count);

    // The calling thread takes the first chunk instead of spinning idle in the
    // wait; with grain >= count that is the whole range, and the pool is unused.
    for (size_t i = 0; i < grain; ++i) {
        invokeAt(i);
    }

    pool.waitForBatch(pending);
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
    // the range inline: grain == count makes the call below submit zero tasks
    // and sweep serially on the calling thread.
    constexpr size_t MIN_PARALLEL = 2048;

    // The +1 is for the main thread.
    const size_t grain = (count < MIN_PARALLEL)
        ? count
        : count / (pool.threadCount() + 1);

    parallelFor(count, grain, function);
}

} // namespace Engine
