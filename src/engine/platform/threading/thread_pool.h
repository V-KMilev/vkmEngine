#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @brief Per-worker queue structure for work-stealing thread pool.
 *
 * @details Each worker thread has its own local queue to minimize contention.
 * The queue uses LIFO (Last-In-First-Out) order for local access to improve
 * cache locality, while work-stealing uses FIFO from the back.
 */
struct WorkerQueue {
    std::mutex mutex;                           ///< Mutex protecting the queue
    std::deque<std::function<void()>> queue;    ///< Deque of pending tasks
    std::atomic<size_t> sizeHint{0};            ///< Approximate size for fast empty check
};

/**
 * @brief Work-stealing thread pool implementation.
 * 
 * @details This thread pool uses a work-stealing algorithm where each worker thread maintains
 * its own local queue. Tasks are preferentially added to the calling thread's local
 * queue (if it's a worker thread), otherwise to a global queue. Workers steal tasks
 * from other workers' queues when their own queue is empty, reducing contention.
 * 
 * The pool is a singleton accessed via get(). It automatically creates worker threads
 * based on hardware concurrency.
 */
class ThreadPool {
    public:
        ThreadPool() = delete;

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

    public:
        /**
         * @brief Gets the singleton thread pool instance.
         * @details Creates the pool on first call with thread count equal to hardware concurrency.
         * @return Reference to the singleton ThreadPool instance
         */
        static ThreadPool& get();
        
        /**
         * @brief Returns the number of worker threads in the pool.
         * @return Number of worker threads
         */
        size_t size() const noexcept { return m_workers.size(); }

        /**
         * @brief Enqueues a task for execution (fire-and-forget).
         * @details If called from a worker thread, the task is added to that thread's local queue.
         * Otherwise, it's added to the global queue. Uses LIFO order for local queues to improve
         * cache locality.
         * @param task The function to execute
         */
        void enqueue(std::function<void()> task);

        /**
         * @brief Submits a task and returns a future to track its completion.
         * @tparam Function Callable type
         * @tparam Arguments Argument types for the function
         * @param function The function to execute
         * @param arguments Arguments to pass to the function
         * @return Future that will contain the result when the task completes
         */
        template <class Function, class... Arguments>
        auto submit(Function&& function, Arguments&&... arguments)
            -> std::future<std::invoke_result_t<Function, Arguments...>>;

        /**
         * @brief Blocks until all queued and currently running tasks have completed.
         * @details This is useful for synchronization before shutdown or when you need to ensure
         * all work is done before proceeding.
         */
        void waitIdle();

        /**
         * @brief Executes a function in parallel over a range [begin, end) with work-stealing.
         * @details The range is divided into chunks of 'grain' size, and each chunk is processed
         * by a worker thread. The function is called with (startIndex, endIndex, workerIndex) for each chunk.
         * @tparam Function Callable type that takes (size_t startIndex, size_t endIndex, size_t workerIndex)
         * @param begin Start of the range (inclusive)
         * @param end End of the range (exclusive)
         * @param grain Size of each work chunk
         * @param function Function to execute for each chunk
         */
        template <class Function>
        void parallelFor(size_t begin, size_t end, size_t grain, Function&& function);

        /**
         * @brief Returns the index of the current worker thread, or -1 if called from a non-worker thread.
         * @details This can be used to identify which worker thread is executing the current task.
         * @return Worker thread index (0 to size()-1) or -1 for external threads
         */
        static int workerIndex() noexcept;

    private:
        /**
         * @brief Constructs a thread pool with the specified number of worker threads.
         * @param threadCount Number of worker threads to create (must be > 0)
         */
        explicit ThreadPool(size_t threadCount);
        /**
         * @brief Destructor stops all worker threads and waits for them to finish.
         */
        ~ThreadPool();

    private:
        /**
         * @brief Gets a reference to a worker's local queue.
         * @param workerIndex Worker thread index
         * @return Reference to the worker's queue
         */
        WorkerQueue& localQueue(size_t workerIndex) { return *m_localQueues[workerIndex]; }
        
        /**
         * @brief Gets a const reference to a worker's local queue.
         * @param workerIndex Worker thread index
         * @return Const reference to the worker's queue
         */
        const WorkerQueue& localQueue(size_t workerIndex) const { return *m_localQueues[workerIndex]; }

        /**
         * @brief Attempts to pop a task from a worker's local queue (LIFO).
         * @param workerIndex Worker thread index
         * @param task Output parameter for the popped task
         * @return true if a task was popped, false if the queue was empty
         */
        bool popLocal(size_t workerIndex, std::function<void()>& task);
        
        /**
         * @brief Attempts to steal a task from another worker's queue (FIFO from back).
         * @param thiefIndex Index of the worker attempting to steal
         * @param task Output parameter for the stolen task
         * @return true if a task was stolen, false if no tasks were available
         */
        bool steal(size_t thiefIndex, std::function<void()>& task);
        
        /**
         * @brief Attempts to pop a task from the global queue (FIFO).
         * @param task Output parameter for the popped task
         * @return true if a task was popped, false if the queue was empty
         */
        bool popGlobal(std::function<void()>& task);
        
        /**
         * @brief Pushes a task onto the global queue.
         * @param task The task to enqueue
         */
        void pushGlobal(std::function<void()> task);

        /**
         * @brief Main loop executed by each worker thread.
         * @details Workers attempt to get work from: local queue -> global queue -> steal from others.
         * @param workerIndex Worker thread index
         */
        void workerLoop(size_t workerIndex);

    private:
        std::vector<std::thread> m_workers;
        std::vector<std::unique_ptr<WorkerQueue>> m_localQueues;

        std::mutex m_globalQueueMutex;
        std::deque<std::function<void()>> m_globalQueue;
        std::atomic<size_t> m_globalQueueSize{0};  ///< Fast empty check for global queue

        std::condition_variable m_conditionVariable;
        std::mutex m_conditionMutex;

        std::atomic<bool> m_stop{false};
        std::atomic<std::int64_t> m_inFlightTaskCount{0};
        std::atomic<std::int32_t> m_waitingThreadCount{0};
};


/**
 * @brief Template implementation of submit().
 * @details Wraps the function and arguments in a packaged_task and enqueues it.
 */
template <class Function, class... Arguments>
auto ThreadPool::submit(Function&& function, Arguments&&... arguments) -> std::future<std::invoke_result_t<Function, Arguments...>> {
    using Result = std::invoke_result_t<Function, Arguments...>;

    auto packagedTask = std::make_shared<std::packaged_task<Result()>>(
        [function = std::forward<Function>(function),
         arguments = std::make_tuple(std::forward<Arguments>(arguments)...)]() mutable -> Result {
            return std::apply(std::move(function), std::move(arguments));
        });

    std::future<Result> future = packagedTask->get_future();
    enqueue([packagedTask]() { (*packagedTask)(); });
    return future;
}

/**
 * @brief Template implementation of parallelFor().
 * @details Pushes tasks directly to worker local queues (round-robin) to avoid
 * global queue contention. Workers grab chunks via atomic counter.
 */
template <class Function>
void ThreadPool::parallelFor(size_t begin, size_t end, size_t grain, Function&& function) {
    if (begin >= end) return;
    if (grain == 0) grain = 1;

    std::atomic<size_t> nextIndex{begin};
    const size_t workerCount = size();

    // Push tasks directly to worker local queues (avoids global queue bottleneck)
    for (size_t i = 0; i < workerCount; ++i) {
        std::function<void()> task = [&, i] {
            while (true) {
                size_t startIndex = nextIndex.fetch_add(grain, std::memory_order_relaxed);
                if (startIndex >= end) break;

                size_t endIndex = std::min(startIndex + grain, end);
                function(startIndex, endIndex, i);
            }
        };

        m_inFlightTaskCount.fetch_add(1, std::memory_order_seq_cst);

        // Push directly to worker i's local queue
        WorkerQueue& workerQueue = localQueue(i);
        {
            std::lock_guard<std::mutex> lock(workerQueue.mutex);
            workerQueue.queue.emplace_back(std::move(task));
            workerQueue.sizeHint.fetch_add(1, std::memory_order_relaxed);
        }

        // Wake one worker per task (more efficient than notify_all at end)
        m_conditionVariable.notify_one();
    }

    waitIdle();
}
