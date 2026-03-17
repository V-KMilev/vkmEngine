#include "platform/threading/thread_pool.h"

namespace Engine {

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
        m_tasks.emplace_back(std::move(task));
    }

    ++m_taskCount;
    m_tasksCV.notify_one();
}

void ThreadPool::addTasks(std::vector<Task>&& tasks) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        for (auto& task : tasks) {
            m_tasks.emplace_back(std::move(task));
        }
    }

    m_taskCount += tasks.size();
    m_tasksCV.notify_all();
}

void ThreadPool::waitToFinish() {
    while (m_taskCount > 0) {
        std::this_thread::yield();
    }
}

void ThreadPool::start(size_t threadCount) {
    for (size_t i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this]() { process(); });
    }
}

void ThreadPool::stop() {
    m_running = false;
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
    while (m_running) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_tasksMutex);
            m_tasksCV.wait(lock, [this]() {
                return !m_tasks.empty() || !m_running;
            });

            if (m_tasks.empty() || !m_running) continue;

            task = m_tasks.front();
            m_tasks.pop_front();
        };

        task.execute();
        --m_taskCount;
    }
}

} // namespace Engine