#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

#include "logger.h"

/**
 * @brief A thread pool for parallel task execution
 * 
 * The ThreadPool class manages a pool of worker threads that can execute
 * tasks concurrently. Tasks are submitted to a queue and executed by
 * available worker threads.
 */
class ThreadPool {
    public:
        ThreadPool() = delete;

        ThreadPool(const ThreadPool& other) = delete;
        ThreadPool& operator=(const ThreadPool& other) = delete;

        ThreadPool(ThreadPool&& other) = delete;
        ThreadPool& operator=(ThreadPool&& other) = delete;

    private:
        /**
         * @brief Constructs a thread pool with the specified number of threads
         * 
         * @param numThreads Number of worker threads to create
         */
         explicit ThreadPool(size_t numThreads);
         ~ThreadPool();

    public:
        /**
         * @brief Get the singleton instance of the ThreadPool.
         * @return Reference to the ThreadPool singleton.
         */
        static ThreadPool& get();

        /**
         * @brief Push a task to be executed by one of the worker threads
         * 
         * @param func Function to execute
         * @param args Arguments to pass to the function
         * @return A future that can be used to get the result of the task once it's completed
         */
        template <typename Func, typename... Args>
        auto push(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>;

        /**
         * @brief Waits for all currently queued tasks to complete
         */
        void waitAll();

        /**
         * @brief Get the number of threads in the thread pool
         * @return The number of threads in the thread pool
         */
        size_t size() const { return m_workers.size(); }

    private:
        /**
         * @brief The worker thread function that continuously pulls tasks from the queue and executes them
         */
        void workerThread();

    private:
        // Pool of worker threads
        std::vector<std::thread> m_workers;
        // Queue of tasks that will be executed by the worker threads
        std::queue<std::function<void()>> m_tasks;
        // Atomic counter to track the number of active tasks
        std::atomic<size_t> m_activeTasks{0};
        // Mutex to protect access to the task queue
        std::mutex m_queueMutex;
        // Condition variable to notify worker threads when a new task is available
        std::condition_variable m_condition;
        // Condition variable to notify the main thread when all tasks are completed
        std::condition_variable m_completionCondition;
        // Flag indicating whether the thread pool is being shut down
        bool m_stop = false;
};

template <typename Func, typename... Args>
auto ThreadPool::push(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
    using ReturnType = decltype(func(args...));  // Deduce the return type of the function

    // Create a shared pointer to a packaged_task, which will be responsible for calling the task
    auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<Func>(func), std::forward<Args>(args)...) // Bind the function and its arguments
    );

    // Retrieve a future from the packaged task, which can be used to obtain the result of the task
    std::future<ReturnType> result = taskPtr->get_future();

    // Lock the queue mutex to safely add the task to the queue
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);

        // If the pool is stopped, we can't enqueue new tasks
        if (m_stop) {
            LOG_ERROR("Cannot enqueue on stopped ThreadPool");
            return std::future<ReturnType>();  // Return an empty future
        }

        // Add the task to the queue
        m_tasks.emplace([taskPtr]() { (*taskPtr)(); });
    }

    // Notify one worker thread that there is a new task available
    m_condition.notify_one();

    // Return the future to the caller
    return result;
}
