#include "event/event_system.h"

#include "logger.h"

#include "debug/print_helper.h"
#include "debug/statistics.h"
#include "event/event_listener.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

EventSystem::EventSystem() {
    LOG_TRACE("Created event manager");
}

EventSystem::~EventSystem() {
    ThreadPool::get().waitIdle();

    LOG_TRACE("Destroyed event manager");
}

void EventSystem::update(FrameContext&) {
    executeAsync();
}

void EventSystem::push(Event && event) {
    // Execute IMMEDIATE events outside the lock to prevent deadlock
    // if the callback calls push() again.
    if (event.getPriority() == EventPriority::IMMEDIATE) {
        event.execute();
        return;
    }

    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_events.emplace(std::move(event));
}

void EventSystem::execute() {
    std::priority_queue<Event> localEvents;

    // Swap queues to avoid long locks
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        std::swap(localEvents, m_events);
    }

    if (localEvents.empty()) {
        return;
    }

    while (!localEvents.empty()) {
        localEvents.top().execute();
        localEvents.pop();
    }
}

void EventSystem::executeAsync() {
    std::priority_queue<Event> localEvents;

    // Swap queues to avoid long locks
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        std::swap(localEvents, m_events);
    }

    if (localEvents.empty()) {
        return;
    }

    // Execute events in priority order on the calling thread
    while (!localEvents.empty()) {
        localEvents.top().execute();
        localEvents.pop();
    }
}

uint32_t EventSystem::subscribe(Event&& event) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);

    const std::string& eventName = event.getName();
    const EventCallback& callback = event.getCallback();

    if (!callback) {
        LOG_WARNING("[EVENT MANAGER] Cannot subscribe - Event '%s' has no callback", eventName.c_str());
        return 0;
    }

    // Generate unique listener ID (thread-safe with lock held)
    uint32_t id = m_nextListenerId++;

    // Store ID -> name mapping for fast unsubscribe
    m_listenerIdToName[id] = eventName;

    // Create EventListener with ID and store it (move)
    m_listeners[eventName].emplace_back(std::move(event), id);

    STATS_RECORD_EVENT_SUBSCRIBE();
    LOG_VERBOSE("[EVENT MANAGER] Subscribed listener #%u to event '%s'", id, eventName.c_str());
    return id;
}

bool EventSystem::unsubscribe(uint32_t id) {
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

            STATS_RECORD_EVENT_UNSUBSCRIBE();
            LOG_VERBOSE("[EVENT MANAGER] Unsubscribed listener #%u from event '%s'", id, eventName.c_str());
        }
    }

    // Remove from ID mapping
    m_listenerIdToName.erase(idIt);
    return true;
}

void EventSystem::unsubscribeAll(const std::string& eventName) {
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

void EventSystem::emit(const std::string& eventName) {
    std::vector<EventCallback> callbacksToCall;

    // Copy callbacks under lock, then release before execution.
    // We copy std::function objects (not pointers) because a callback
    // may subscribe/unsubscribe, which can reallocate the listener vector.
    {
        std::lock_guard<std::mutex> lock(m_listenerMutex);

        auto it = m_listeners.find(eventName);
        if (it == m_listeners.end() || it->second.empty()) {
            LOG_VERBOSE("[EVENT MANAGER] No listeners for event type '%s'", eventName.c_str());
            return;
        }

        std::vector<EventListener>& listeners = it->second;

        LOG_VERBOSE("[EVENT MANAGER] Emitting event '%s' to %zu listener(s)",
                    eventName.c_str(), listeners.size());

        if (listeners.size() > 1) {
            std::sort(listeners.begin(), listeners.end(),
                [](const EventListener& a, const EventListener& b) {
                    return b < a;  // Descending priority (uses operator<)
                });
        }

        callbacksToCall.reserve(listeners.size());
        for (const EventListener& listener : listeners) {
            callbacksToCall.push_back(listener.getCallback());
        }
    }

    // Execute without holding the mutex - safe even if callbacks subscribe/unsubscribe
    for (const EventCallback& callback : callbacksToCall) {
        callback();
    }
    STATS_RECORD_EVENT_DISPATCH();
}

void EventSystem::emitAsync(const std::string& eventName) {
    std::vector<EventCallback> callbacksToCall;

    // Copy callbacks under lock (same safety rationale as emit)
    {
        std::lock_guard<std::mutex> lock(m_listenerMutex);

        auto it = m_listeners.find(eventName);
        if (it == m_listeners.end() || it->second.empty()) {
            LOG_VERBOSE("[EVENT MANAGER] No listeners for event type '%s'", eventName.c_str());
            return;
        }

        std::vector<EventListener>& listeners = it->second;

        LOG_VERBOSE("[EVENT MANAGER] Emitting async event '%s' to %zu listener(s)",
                    eventName.c_str(), listeners.size());

        if (listeners.size() > 1) {
            std::sort(listeners.begin(), listeners.end(),
                [](const EventListener& a, const EventListener& b) {
                    return b < a;  // Descending priority (uses operator<)
                });
        }

        callbacksToCall.reserve(listeners.size());
        for (const EventListener& listener : listeners) {
            callbacksToCall.push_back(listener.getCallback());
        }
    }

    for (const EventCallback& callback : callbacksToCall) {
        callback();
    }
}

} // namespace Engine
