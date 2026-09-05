#define VKM_LOG_CATEGORY "THREAD"

#include "platform/threading/thread_pool.h"

#include <exception>

#include "logger.h"

namespace Vkm::Engine {

namespace {
// Set inside process() so parallelFor can detect re-entry from a worker
// and fall back to a serial sweep instead of deadlocking on its own slot.
thread_local bool t_isWorker = false;
} // namespace

bool ThreadPool::isWorkerThread() {
    return t_isWorker;
}

ThreadPool::ThreadPool(
    size_t threadCount
) : m_running(true) {
    for (size_t i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this]() { process(); });
    }
}
ThreadPool::~ThreadPool() {
    shutdown();
}

ThreadPool& ThreadPool::get() {
    // hardware_concurrency() is allowed to answer 0 when it cannot tell. One
    // worker still drains the queue; zero would strand every task in it.
    static ThreadPool s_instance(std::max<size_t>(1, std::thread::hardware_concurrency()));
    return s_instance;
}

void ThreadPool::addTask(std::function<void()> && task) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        // Nothing drains the queue once the pool is down, so an enqueue there
        // is work silently never done. Refused and said so - unlike parallelFor,
        // which can still run the loop on the thread that asked.
        if (!m_running) {
            LOG_WARNING("ThreadPool::addTask after shutdown; the task is dropped");
            return;
        }
        m_backgroundTasks.push_back(QueuedTask{std::move(task), nullptr});
    }

    m_tasksCV.notify_one();
}

void ThreadPool::addTasks(std::vector<std::function<void()>> && tasks, std::atomic<size_t>& pending) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        pending += tasks.size();
        for (auto& task : tasks) {
            m_frameTasks.push_back(QueuedTask{std::move(task), &pending});
        }
    }

    m_tasksCV.notify_all();
}

void ThreadPool::waitForBatch(std::atomic<size_t>& pending) {
    // A batch counter only ever falls, so a zero read here is final and needs
    // no lock - that is the whole cost of a parallelFor that submitted nothing.
    if (pending.load() == 0) return;

    std::unique_lock<std::mutex> lock(m_tasksMutex);
    m_doneCV.wait(lock, [&pending]() { return pending.load() == 0; });
}

void ThreadPool::shutdown() {
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
    m_frameTasks.clear();
    m_backgroundTasks.clear();
}

void ThreadPool::process() {
    t_isWorker = true;
    while (m_running) {
        QueuedTask queued;
        {
            std::unique_lock<std::mutex> lock(m_tasksMutex);
            m_tasksCV.wait(lock, [this]() {
                return !m_frameTasks.empty() || !m_backgroundTasks.empty() || !m_running;
            });

            if (!m_running) continue;

            // The frame first, and only then what nobody is waiting for. See
            // the class comment for why this cannot starve a decode.
            std::deque<QueuedTask>& queue =
                !m_frameTasks.empty() ? m_frameTasks : m_backgroundTasks;
            if (queue.empty()) continue;

            queued = std::move(queue.front());
            queue.pop_front();
        }

        // A throwing task must NOT skip the retire below: waitForBatch() would
        // block forever on a count that never reaches zero. Swallow and log;
        // task bodies are responsible for their own error reporting.
        try {
            queued.function();
        } catch (const std::exception& e) {
            LOG_ERROR("ThreadPool task threw: %s", e.what());
        } catch (...) {
            LOG_ERROR("ThreadPool task threw unknown exception");
        }

        if (!queued.pending) continue;

        // Under the queue lock, so a waiter evaluating its predicate cannot
        // miss the drop to zero and sleep through the notify.
        size_t remaining;
        {
            std::lock_guard<std::mutex> lock(m_tasksMutex);
            remaining = --(*queued.pending);
        }
        if (remaining == 0) m_doneCV.notify_all();
    }
}

} // namespace Vkm::Engine
