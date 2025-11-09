#include "event_manager.h"

#include "logger.h"
#include "print_helper.h"
#include "event_listener.h"

EventManager::EventManager(size_t numThreads) : m_threadPool(numThreads) {
    LOG_TRACE("Created event manager with %d threads", numThreads);
}

EventManager& EventManager::get() {
    static EventManager instance;
    return instance;
}

void EventManager::push(Event && event) {
    std::lock_guard<std::mutex> lock(m_eventQueueMutex);

    if (event.getPriority() == EventPriority::IMMEDIATE) {
        m_threadPool.push([this, event = std::move(event)] {
            event.execute();
        });
        return;
    }

    m_queue.emplace(std::move(event));
}

void EventManager::execute() {
    static std::priority_queue<Event> localQueue;

    // Swap queues to avoid long locks
    {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
        std::swap(localQueue, m_queue);
    }

    if (localQueue.empty()) {
        return;
    }

    while (!localQueue.empty()) {
        localQueue.top().execute();
        localQueue.pop();
    }
}

void EventManager::executeAsync() {
    static std::priority_queue<Event> localQueue;

    // Swap queues to avoid long locks
    {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
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
        m_threadPool.push([event = std::move(event)]() {
            event.execute();
        });
    }
}

uint32_t EventManager::subscribe(Event&& event) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);

    const std::string& eventName = event.getName();
    const EventCallback& callback = event.getCallback();

    if (!callback) {
        LOG_WARNING("[EVENT MANAGER] Cannot subscribe - Event '%s' has no callback", eventName.c_str());
        return 0;
    }

    // Generate unique listener ID
    static uint32_t id = 0;
    id++;

    // Store ID -> name mapping for fast unsubscribe
    m_listenerIdToName[id] = eventName;

    // Create EventListener with ID and store it (move)
    m_listeners[eventName].emplace_back(std::move(event), id);

    LOG_VERBOSE("[EVENT MANAGER] Subscribed listener #%u to event '%s'", id, eventName.c_str());
    return id;
}

bool EventManager::unsubscribe(uint32_t id) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);

    // O(1) lookup of event name from ID
    auto idIt = m_listenerIdToName.find(id);
    if (idIt == m_listenerIdToName.end()) {
        LOG_WARNING("[EVENT MANAGER] Cannot unsubscribe - Listener ID:%u not found", id);
        return false;
    }

    const std::string& eventName = idIt->second;

    // Find the listener vector for this event
    auto listenersIt = m_listeners.find(eventName);
    if (listenersIt != m_listeners.end()) {
        auto& listeners = listenersIt->second;

        // Remove listener with matching ID (single pass)
        auto it = std::remove_if(listeners.begin(), listeners.end(),
            [id](const EventListener& listener) {
                return listener.getID() == id;
            });

        if (it != listeners.end()) {
            listeners.erase(it, listeners.end());

            // If no more listeners for this event, remove the entry
            if (listeners.empty()) {
                m_listeners.erase(listenersIt);
            }

            LOG_VERBOSE("[EVENT MANAGER] Unsubscribed listener #%u from event '%s'", id, eventName.c_str());
        }
    }

    // Remove from ID mapping
    m_listenerIdToName.erase(idIt);
    return true;
}

void EventManager::unsubscribeAll(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);

    auto it = m_listeners.find(eventName);
    if (it == m_listeners.end()) {
        LOG_VERBOSE("[EVENT MANAGER] No listeners to unsubscribe for event '%s'", eventName.c_str());
        return;
    }

    // Remove all listener IDs from the ID->name mapping
    for (const auto& listener : it->second) {
        m_listenerIdToName.erase(listener.getID());
    }

    size_t count = it->second.size();
    m_listeners.erase(it);

    LOG_VERBOSE("[EVENT MANAGER] Unsubscribed all %zu listener(s) from event '%s'", 
                count, eventName.c_str());
}

void EventManager::emit(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);

    auto it = m_listeners.find(eventName);
    if (it == m_listeners.end() || it->second.empty()) {
        LOG_VERBOSE("[EVENT MANAGER] No listeners for event type '%s'", eventName.c_str());
        return;
    }

    LOG_VERBOSE("[EVENT MANAGER] Emitting event '%s' to %zu listener(s)", eventName.c_str(), it->second.size());

    // Sort listeners by priority (highest first)
    // Uses EventListener::operator< (reverse for descending order)
    std::vector<EventListener>& listeners = it->second;
    std::sort(listeners.begin(), listeners.end(), 
        [](const EventListener& a, const EventListener& b) {
            return b < a;  // Reverse comparison for descending priority
        });

    for (const EventListener& listener : listeners) {
        listener.execute();
    }
}

void EventManager::emitAsync(const std::string& eventName) {
    std::vector<const EventListener*> listenersToCall;

    // Collect pointers to listeners (safe because listeners are stable in vector)
    {
        std::lock_guard<std::mutex> lock(m_listenerMutex);

        auto it = m_listeners.find(eventName);
        if (it == m_listeners.end() || it->second.empty()) {
            LOG_VERBOSE("[EVENT MANAGER] No listeners for event type '%s'", eventName.c_str());
            return;
        }

        LOG_VERBOSE("[EVENT MANAGER] Emitting async event '%s' to %zu listener(s)", eventName.c_str(), it->second.size());

        // Sort by priority (highest first)
        // Uses EventListener::operator< (reverse for descending order)
        std::vector<EventListener>& listeners = it->second;
        std::sort(listeners.begin(), listeners.end(), 
            [](const EventListener& a, const EventListener& b) {
                return b < a;  // Reverse comparison for descending priority
            });

        // Collect pointers
        listenersToCall.reserve(listeners.size());
        for (const EventListener& listener : listeners) {
            listenersToCall.push_back(&listener);
        }
    }

    for (const EventListener* listener : listenersToCall) {
        m_threadPool.push([listener]() {
            listener->execute();
        });
    }
}