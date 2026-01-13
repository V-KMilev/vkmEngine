#include "thread_pool.h"

#include "logger.h"

ThreadPool::ThreadPool(size_t numThreads) {
    // Create worker threads and assign them to execute the workerThread function.
    for (size_t i = 0; i < numThreads; ++i) {
        // Each thread runs workerThread.
        m_workers.emplace_back([this]() { this->workerThread(); });
    }
    LOG_TRACE("Created thread pool with %zu threads", m_workers.size());
}

ThreadPool::~ThreadPool() {
    {
        // Lock the queueMutex to safely modify the stop flag.
        std::unique_lock<std::mutex> lock(m_queueMutex);
         // Set the stop flag to true, signaling workers to stop.
        m_stop = true;
    }

    // Notify all worker threads that they should stop processing tasks.
    m_condition.notify_all();

    // Join all threads to ensure they finish execution before the destructor completes.
    for (std::thread& thread : m_workers) {
        // Wait for each thread to finish.
        if (thread.joinable()) thread.join();
    }
    LOG_TRACE("Destroyed thread pool with %zu threads", m_workers.size());
}

// Worker thread function that continuously fetches and processes tasks from the task queue.
void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;

        {
            // Lock the queueMutex to safely access the task queue.
            std::unique_lock<std::mutex> lock(m_queueMutex);

            // Wait for tasks to be available or for the stop flag to be set.
            m_condition.wait(lock, [this]() { return m_stop || !m_tasks.empty(); });

            // If the stop flag is set and there are no tasks, exit the worker thread.
            if (m_stop && m_tasks.empty()) return;

            // Get the task from the front of the queue and remove it.
            task = std::move(m_tasks.front());
            m_tasks.pop();

            // Increment active tasks count.
            ++m_activeTasks;
        }

        // Execute the task.
        task();

        {
            // Lock the queueMutex to update the active tasks count.
            std::unique_lock<std::mutex> lock(m_queueMutex);

            // Decrement the active tasks count after completing the task.
            --m_activeTasks;

            // If all tasks are finished and no tasks are remaining, notify the main thread.
            if (m_tasks.empty() && m_activeTasks == 0) {
                m_completionCondition.notify_all();
            }
        }
    }
}

// Waits for all tasks to be completed before continuing.
void ThreadPool::waitAll() {
    // Lock the queueMutex to safely check the state of tasks and active tasks.
    std::unique_lock<std::mutex> lock(m_queueMutex);

    // Wait for all tasks to be processed and the active tasks counter to reach zero.
    m_completionCondition.wait(lock, [this]() {
        // Wait until no tasks are pending and no tasks are active.
        return m_tasks.empty() && m_activeTasks == 0;
    });
    LOG_TRACE("Thread pool successfully stopped!");
}
