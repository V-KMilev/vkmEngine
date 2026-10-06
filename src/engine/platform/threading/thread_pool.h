#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Fixed-size pool of worker threads draining two task queues.
 *
 * Waiting is per-batch, so a caller never blocks on work someone else queued. Batches (the frame
 * is blocked on them) are served before addTask's background tasks. That cannot starve the
 * background for long: only the frame fills the batch queue, and a batch must be joined within
 * the frame that added it.
 */
class ThreadPool {
    public:
        /**
         * @brief A run of tasks addressed by index, queued as one entry.
         *
         * The task is borrowed, not copied, so a batch of any size allocates nothing. The batch
         * and its task must outlive the matching waitForBatch().
         */
        class Batch {
            public:
                /**
                 * @brief A batch of @p count tasks, each a call of @p task with its index.
                 *
                 * @tparam Fn Callable taking a size_t index.
                 * @param count How many indices, counted from 0.
                 * @param task Called once per index, on a worker or in waitForBatch; borrowed, not copied.
                 */
                template<typename Fn>
                Batch(size_t count, const Fn& task)
                    : m_run([](const void* body, size_t index) { (*static_cast<const Fn*>(body))(index); })
                    , m_task(&task)
                    , m_count(count)
                {}

                /// Refused: a temporary task is gone before a worker reaches it.
                template<typename Fn>
                Batch(size_t count, const Fn && task) = delete;

                ~Batch() = default;

                Batch(const Batch& other) = delete;
                Batch& operator=(const Batch& other) = delete;

                Batch(Batch && other) = delete;
                Batch& operator=(Batch && other) = delete;

            private:
                friend class ThreadPool;

            private:
                void (*m_run)(const void* task, size_t index);
                const void* m_task;
                size_t      m_count;
                size_t      m_claimed = 0;          ///< Indices handed out; guarded by the pool's mutex.
                std::exception_ptr m_error;         ///< First an index threw; guarded by the pool's mutex.

                std::atomic<size_t> m_pending{0};   ///< Indices queued and not yet retired.
        };

    public:
        ThreadPool(const ThreadPool& other) = delete;
        ThreadPool& operator=(const ThreadPool& other) = delete;

        ThreadPool(ThreadPool && other) = delete;
        ThreadPool& operator=(ThreadPool && other) = delete;

    public:
        /**
         * @brief Access the process-wide thread pool, constructed on first use.
         *
         * @return The single pool.
         */
        static ThreadPool& get();

        /**
         * @brief Enqueue a single task and wake one worker.
         *
         * Refused with a warning after shutdown(), when nothing drains the queue.
         *
         * @param task The work to run on a worker thread.
         */
        void addTask(std::function<void()> && task);

        /**
         * @brief Queue every index of @p batch as one entry and wake all workers.
         *
         * Refused after shutdown() with the counter left at zero, so waitForBatch() returns.
         *
         * @param batch The batch to run; it and its task must outlive waitForBatch().
         */
        void addBatch(Batch& batch);

        /**
         * @brief Block the caller until every index of @p batch has retired.
         *
         * The caller first runs every index no worker has claimed. An index that throws still
         * retires; the first exception thrown is rethrown here, whichever thread ran it.
         *
         * @param batch The batch handed to addBatch.
         */
        void waitForBatch(Batch& batch);

        /**
         * @brief Join the workers now, ahead of the pool's own destruction.
         *
         * Started tasks finish; queued ones are dropped but still retire, so no waiter blocks.
         * Call before main returns: the static's destructor runs after the singletons tasks write
         * into, and on Windows inside DLL unload, where a join hangs. Idempotent.
         */
        void shutdown();

        /**
         * @brief True when called from a thread owned by the pool.
         *
         * parallelFor sweeps serially on a worker. Not for safety (a nested call would finish):
         * the other threads are busy, so nested chunks would only come back at a lock each.
         *
         * @return True on a pool worker, false on any other thread.
         */
        static bool isWorkerThread();

        size_t threadCount() const { return m_threads.size(); }

    private:
        /**
         * @brief One unit of queued work: an owned task, or an index of a Batch.
         *
         * A batch stays one queue entry until its last index is claimed.
         */
        struct QueuedTask {
            std::function<void()> function;           ///< When not a batch's.
            Batch*                batch   = nullptr;
            size_t                index   = 0;        ///< Once claimed.
        };

    private:
        ThreadPool(size_t threadCount);
        ~ThreadPool();

        /**
         * @brief Worker loop: pop and run tasks until shutdown.
         */
        void process();

        /**
         * @brief Run one claimed index of @p batch and retire it, even when it throws.
         *
         * The first throw is kept on the batch for waitForBatch to rethrow.
         *
         * @param batch The batch the index was claimed from.
         * @param index The claimed index.
         */
        void runIndex(Batch& batch, size_t index);

        /**
         * @brief Drop @p pending by one finished task, waking waiters when it empties.
         *
         * The drop to zero takes the queue lock so the notify cannot land between a waiter reading
         * nonzero and sleeping. The counter is untouched after the decrement: its owner may be gone.
         *
         * @param pending The batch counter the task was queued against.
         */
        void retire(std::atomic<size_t>& pending);

    private:
        std::atomic<bool> m_running;

        std::vector<std::thread> m_threads;

        std::deque<QueuedTask> m_frameTasks;       ///< From addBatch; drained first.
        std::deque<QueuedTask> m_backgroundTasks;  ///< From addTask; drained when idle.

        std::mutex m_tasksMutex;
        std::condition_variable m_tasksCV;
        std::condition_variable m_doneCV;   ///< A batch counter dropped to 0
};

/**
 * @brief Run @p function over [0, count) in @p grain-sized chunks across the pool, the caller
 *        running the first.
 *
 * Serial on a worker thread. Blocks until done, then rethrows the caller's chunk's exception,
 * else the first a queued chunk threw.
 *
 * @tparam Function Callable taking a size_t index, or no arguments.
 * @param count    Number of indices, counted from 0.
 * @param grain    Indices per chunk; 0 is taken as 1.
 * @param function Called once per index, on whichever thread runs its chunk.
 */
template<class Function>
void parallelFor(size_t count, size_t grain, Function&& function) {
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

    if (ThreadPool::isWorkerThread()) {
        for (size_t i = 0; i < count; ++i) invokeAt(i);
        return;
    }

    auto& pool = ThreadPool::get();

    // No workers after shutdown (or with no cores reported): a submission would never retire.
    if (pool.threadCount() == 0) {
        for (size_t i = 0; i < count; ++i) invokeAt(i);
        return;
    }

    // Chunk 0 is the caller's; batch index i is chunk i + 1.
    const size_t chunks = (count - 1) / grain + 1;
    const auto runChunk = [&](size_t chunk) {
        const size_t end = std::min(count, (chunk + 1) * grain);
        for (size_t index = chunk * grain; index < end; ++index) invokeAt(index);
    };
    const auto runQueued = [&](size_t task) { runChunk(task + 1); };
    ThreadPool::Batch batch(chunks - 1, runQueued);

    pool.addBatch(batch);

    // Queued indices reach into this frame, so join before the caller's exception leaves it;
    // that exception, not a queued chunk's, propagates.
    try {
        runChunk(0);
    } catch (...) {
        try {
            pool.waitForBatch(batch);
        } catch (...) {
        }
        throw;
    }
    pool.waitForBatch(batch);
}

/**
 * @brief parallelFor with an auto-chosen grain, inline below MIN_PARALLEL items.
 *
 * Larger ranges split evenly across the workers plus the calling thread.
 *
 * @tparam Function Callable taking a size_t index, or no arguments.
 * @param count    Number of indices, counted from 0.
 * @param function Called once per index, on whichever thread runs its chunk.
 */
template<class Function>
void parallelFor(size_t count, Function&& function) {
    auto& pool = ThreadPool::get();

    // Below this the dispatch cost (mutex, notify_all, done-CV round trip) dwarfs the work;
    // grain == count sweeps serially.
    constexpr size_t MIN_PARALLEL = 2048;

    // The +1 is the calling thread, which runs a chunk too.
    const size_t grain = (count < MIN_PARALLEL)
        ? count
        : count / (pool.threadCount() + 1);

    parallelFor(count, grain, function);
}

} // namespace Vkm::Engine
