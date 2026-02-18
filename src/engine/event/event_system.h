#pragma once

#include <unordered_map>
#include <string>
#include <queue>
#include <mutex>

#include "event/event_listener.h"
#include "event/event.h"
#include "core/system.h"

namespace Engine {

/**
 * @class EventSystem
 * @brief Central class for managing, dispatching, and subscribing events.
 *
 * Supports two major event systems:
 *  - Push/pop queue system for simple, prioritized events.
 *  - Named listener subscription system for event pub/sub.
 * Thread-safe and executes async events on a dedicated thread pool.
 */
class EventSystem : public System {
    public:
        EventSystem();
        ~EventSystem() override;

        EventSystem(const EventSystem& other) = delete;
        EventSystem& operator=(const EventSystem& other) = delete;

        EventSystem(EventSystem && other) = delete;
        EventSystem& operator=(EventSystem && other) = delete;

    public:
        /**
         * @brief Per-frame system update — executes queued events.
         * @param ctx The shared FrameContext for this frame.
         */
        void update(FrameContext& ctx) override;

        /**
         * @brief Push an event into the queue. Immediate events are executed on a thread.
         * @param event Event to queue or execute.
         */
        void push(Event && event);

        /**
         * @brief Execute all queued events synchronously (main thread).
         */
        void execute();

        /**
         * @brief Execute all queued events asynchronously (thread pool).
         */
        void executeAsync();

        /**
         * @brief Subscribe a listener callback for a given named event.
         * @param event Event, carrying callback, name and priority.
         * @return Listener ID (for unsubscription); returns 0 for invalid callback.
         */
        uint32_t subscribe(Event && event);

        /**
         * @brief Unsubscribe a listener by its unique ID.
         * @param id Listener ID.
         * @return true if unsubscribed, false if not found.
         */
        bool unsubscribe(uint32_t id);

        /**
         * @brief Unsubscribe all listeners from a named event.
         * @param eventName Name of the event.
         */
        void unsubscribeAll(const std::string& eventName);

        /**
         * @brief Emit a named event, invoking all subscribers synchronously (sorted by priority).
         * @param eventName Name of the event.
         */
        void emit(const std::string& eventName);

        /**
         * @brief Emit a named event to all subscribers asynchronously (thread pool).
         * @param eventName Name of the event.
         */
        void emitAsync(const std::string& eventName);

    private:
        std::priority_queue<Event> m_events;
        std::mutex m_eventMutex;

        std::unordered_map<std::string, std::vector<EventListener>> m_listeners;
        std::mutex m_listenerMutex;

        std::unordered_map<uint32_t, std::string> m_listenerIdToName;
        uint32_t m_nextListenerId = 1;
};

} // namespace Engine
