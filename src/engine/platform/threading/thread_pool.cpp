#include "platform/threading/thread_pool.h"

#include <algorithm>

// Thread-local storage to track which worker thread is executing
static thread_local int g_workerIndex = -1;

namespace Engine {

ThreadPool::ThreadPool(size_t threadCount) {
    // Ensure at least one thread
    if (threadCount == 0) {
        threadCount = 1;
    }

    // Create local queues for each worker thread
    m_localQueues.reserve(threadCount);
    for (size_t index = 0; index < threadCount; ++index) {
        m_localQueues.emplace_back(std::make_unique<WorkerQueue>());
    }

    // Create and start worker threads
    m_workers.reserve(threadCount);
    for (size_t index = 0; index < threadCount; ++index) {
        m_workers.emplace_back([this, index] {
            workerLoop(index);
        });
    }
}

ThreadPool::~ThreadPool() {
    // Signal all workers to stop
    m_stop.store(true, std::memory_order_release);
    m_conditionVariable.notify_all();

    // Wait for all worker threads to finish
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

ThreadPool& ThreadPool::get() {
    static ThreadPool pool(std::thread::hardware_concurrency());
    return pool;
}

int ThreadPool::workerIndex() noexcept {
    return g_workerIndex;
}

void ThreadPool::pushGlobal(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(m_globalQueueMutex);
    m_globalQueue.emplace_back(std::move(task));
    m_globalQueueSize.fetch_add(1, std::memory_order_relaxed);
}

void ThreadPool::enqueue(std::function<void()> task) {
    // Increment in-flight task count BEFORE adding to queue
    // Use seq_cst to ensure visibility across threads
    m_inFlightTaskCount.fetch_add(1, std::memory_order_seq_cst);

    // If called from a worker thread, add to its local queue (LIFO for cache efficiency)
    const int currentWorkerIndex = workerIndex();
    if (currentWorkerIndex >= 0) {
        WorkerQueue& workerQueue = localQueue(static_cast<size_t>(currentWorkerIndex));
        {
            std::lock_guard<std::mutex> lock(workerQueue.mutex);
            workerQueue.queue.emplace_front(std::move(task)); // LIFO
            workerQueue.sizeHint.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        // External thread: add to global queue
        pushGlobal(std::move(task));
    }

    // Always notify under lock to ensure proper synchronization
    {
        std::lock_guard<std::mutex> lock(m_conditionMutex);
    }
    m_conditionVariable.notify_one();
}

bool ThreadPool::popLocal(size_t workerIndex, std::function<void()>& task) {
    WorkerQueue& workerQueue = localQueue(workerIndex);

    // Fast path: check size hint before locking
    if (workerQueue.sizeHint.load(std::memory_order_relaxed) == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(workerQueue.mutex);

    if (workerQueue.queue.empty()) {
        return false;
    }

    // Pop from front (LIFO order)
    task = std::move(workerQueue.queue.front());
    workerQueue.queue.pop_front();
    workerQueue.sizeHint.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool ThreadPool::popGlobal(std::function<void()>& task) {
    // Fast path: skip locking if global queue is empty
    if (m_globalQueueSize.load(std::memory_order_relaxed) == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_globalQueueMutex);

    if (m_globalQueue.empty()) {
        return false;
    }

    // Pop from front (FIFO order)
    task = std::move(m_globalQueue.front());
    m_globalQueue.pop_front();
    m_globalQueueSize.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool ThreadPool::steal(size_t thiefIndex, std::function<void()>& task) {
    const size_t workerCount = m_localQueues.size();

    // Need at least 2 workers to steal
    if (workerCount <= 1) {
        return false;
    }

    // Try to steal from other workers using try_lock to avoid blocking
    for (size_t offset = 1; offset < workerCount; ++offset) {
        size_t victimIndex = (thiefIndex + offset) % workerCount;
        WorkerQueue& victimQueue = localQueue(victimIndex);

        // Fast path: skip empty queues without locking
        if (victimQueue.sizeHint.load(std::memory_order_relaxed) == 0) {
            continue;
        }

        // Use try_lock: skip if queue is contested instead of blocking
        std::unique_lock<std::mutex> lock(victimQueue.mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            continue;  // Queue is being used, try next victim
        }

        if (!victimQueue.queue.empty()) {
            // Steal from back (opposite end from owner's LIFO access)
            task = std::move(victimQueue.queue.back());
            victimQueue.queue.pop_back();
            victimQueue.sizeHint.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
    }

    return false;
}

void ThreadPool::workerLoop(size_t workerIndex) {
    // Set thread-local worker index for this thread
    g_workerIndex = static_cast<int>(workerIndex);

    std::function<void()> task;

    while (true) {
        // Try to get work: local queue -> global queue -> steal from others
        if (popLocal(workerIndex, task) ||
            popGlobal(task) ||
            steal(workerIndex, task)) {

            // Execute the task
            task();
            task = {};

            // Decrement in-flight count and notify waiters if this was the last task
            // Use seq_cst for strongest memory ordering guarantee
            if (m_inFlightTaskCount.fetch_sub(1, std::memory_order_seq_cst) == 1) {
                // Wake threads waiting in waitIdle()
                std::lock_guard<std::mutex> lock(m_conditionMutex);
                m_conditionVariable.notify_all();
            }
            continue;
        }

        // No work available - check if we should stop
        if (m_stop.load(std::memory_order_acquire)) {
            // Best-effort drain: try to process remaining global queue tasks
            if (popGlobal(task)) {
                task();
                task = {};
                if (m_inFlightTaskCount.fetch_sub(1, std::memory_order_seq_cst) == 1) {
                    std::lock_guard<std::mutex> lock(m_conditionMutex);
                    m_conditionVariable.notify_all();
                }
                continue;
            }
            // No more work, exit
            return;
        }

        // One yield before sleeping - gives other workers time to finish
        std::this_thread::yield();

        // Try once more after yield
        if (popLocal(workerIndex, task) || steal(workerIndex, task)) {
            task();
            task = {};
            if (m_inFlightTaskCount.fetch_sub(1, std::memory_order_seq_cst) == 1) {
                std::lock_guard<std::mutex> lock(m_conditionMutex);
                m_conditionVariable.notify_all();
            }
            continue;
        }

        // No work and not stopping - wait for work to arrive
        {
            std::unique_lock<std::mutex> lock(m_conditionMutex);
            // Increment waiting count INSIDE the lock to avoid race with enqueue
            m_waitingThreadCount.fetch_add(1, std::memory_order_relaxed);
            m_conditionVariable.wait(lock, [&] {
                return m_stop.load(std::memory_order_acquire) ||
                       m_inFlightTaskCount.load(std::memory_order_acquire) > 0;
            });
            m_waitingThreadCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

void ThreadPool::waitIdle() {
    // Fast path: if no tasks in flight, return immediately
    // Use seq_cst for strongest memory ordering guarantee
    if (m_inFlightTaskCount.load(std::memory_order_seq_cst) == 0) {
        return;
    }

    // Wait until all tasks complete
    std::unique_lock<std::mutex> lock(m_conditionMutex);
    m_conditionVariable.wait(lock, [&] {
        return m_inFlightTaskCount.load(std::memory_order_seq_cst) == 0;
    });
}

} // namespace Engine
