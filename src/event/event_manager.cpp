#include "event_manager.h"

#include "logger.h"
#include "print_helper.h"

EventManager::EventManager(size_t numThreads) : m_threadPool(numThreads) {
    LOG_TRACE("Created event manager with %d threads", numThreads);
}

EventManager& EventManager::get() {
    static EventManager instance;
    return instance;
}

void EventManager::push(Event && event) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (event.getPriority() == EventPriority::IMMEDIATE) {
        m_threadPool.push([this, event = std::move(event)] {
            executeEvent(event);
        });
        return;
    }

    m_queue.emplace(std::move(event));
}

void EventManager::execute() {
    std::priority_queue<Event> localQueue;

    // Swap queues to avoid long locks
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(localQueue, m_queue);
    }

    if (localQueue.empty()) {
        return;
    }

    while (!localQueue.empty()) {
        // priority_queue::top() returns const_reference, so we can't move directly.
        // const_cast to allow move (safe because we pop immediately)
        Event event = std::move(const_cast<Event&>(localQueue.top()));
        localQueue.pop();

        // Capture event by value (it's been moved) so it lives independently in the thread
        m_threadPool.push([this, event = std::move(event)]() {
           executeEvent(event);
        });
        // Single thread execution
        // executeEvent(localQueue.top());
        // localQueue.pop();
    }
}

void EventManager::executeEvent(const Event& event) const {
    const EventCallback& callback = event.getCallback();

    if (!callback) {
        LOG_WARNING("[EVENT MANAGER] No callback found for event '%s' - '%s'", event.getName().c_str(), enumToString(event.getPriority()));
        return;
    }

    try {
        callback();
        LOG_VERBOSE("[EVENT MANAGER] Executed '%s' - '%s'", event.getName().c_str(), enumToString(event.getPriority()));

    } catch (const std::exception& e) {
        LOG_ERROR("[EVENT MANAGER] Exception in event '%s' (%s): %s", 
                  event.getName().c_str(), 
                  enumToString(event.getPriority()),
                  e.what());
    } catch (...) {
        LOG_ERROR("[EVENT MANAGER] Unknown exception in event '%s' (%s)", 
                  event.getName().c_str(), 
                  enumToString(event.getPriority()));
    }
}
