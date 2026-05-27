#define VKM_LOG_CATEGORY "THREAD"

#include "platform/threading/thread_pool.h"

#include <exception>

#include "logger.h"

namespace Engine {

namespace {
    // Set inside process() so parallelFor can detect re-entry from a worker
    // and fall back to a serial sweep instead of deadlocking on its own slot.
    thread_local bool t_isWorker = false;
}

bool ThreadPool::isWorkerThread() {
    return t_isWorker;
}

ThreadPool::ThreadPool(
    size_t threadCount
) : m_running(true),
    m_taskCount(0) {
    start(threadCount);
}
ThreadPool::~ThreadPool() {
    stop();
}

ThreadPool& ThreadPool::get() {
    static ThreadPool instance(DEFAULT_THREAD_COUNT);
    return instance;
}

void ThreadPool::addTask(Task && task) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        ++m_taskCount;
        m_tasks.emplace_back(std::move(task));
    }

    m_tasksCV.notify_one();
}

void ThreadPool::addTasks(std::vector<Task>&& tasks) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_taskCount += tasks.size();
        for (auto& task : tasks) {
            m_tasks.emplace_back(std::move(task));
        }
    }

    m_tasksCV.notify_all();
}

void ThreadPool::waitToFinish() {
    std::unique_lock<std::mutex> lock(m_tasksMutex);
    m_doneCV.wait(lock, [this]() { return m_taskCount == 0; });
}

void ThreadPool::start(size_t threadCount) {
    for (size_t i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this]() { process(); });
    }
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_running = false;
    }
    m_tasksCV.notify_all();

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    m_threads.clear();
    m_tasks.clear();
}

void ThreadPool::process() {
    t_isWorker = true;
    while (m_running) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_tasksMutex);
            m_tasksCV.wait(lock, [this]() {
                return !m_tasks.empty() || !m_running;
            });

            if (m_tasks.empty() || !m_running) continue;

            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }

        // A throwing task must NOT skip the decrement: waitToFinish() would
        // block forever on a count that never reaches zero. Swallow and log;
        // task bodies are responsible for their own error reporting.
        try {
            task.execute();
        } catch (const std::exception& e) {
            LOG_ERROR("ThreadPool task threw: %s", e.what());
        } catch (...) {
            LOG_ERROR("ThreadPool task threw unknown exception");
        }

        size_t remaining;
        {
            std::lock_guard<std::mutex> lock(m_tasksMutex);
            remaining = --m_taskCount;
        }
        if (remaining == 0) m_doneCV.notify_all();
    }
}

} // namespace Engine
