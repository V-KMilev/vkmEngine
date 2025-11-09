#pragma once

#include <queue>
#include <mutex>
#include <string>

#include <unordered_map>
#include <vector>

#include "thread_pool.h"

#include "event_listener.h"
#include "event.h"

#define DEFAULT_EVENT_THREADS 4

class EventManager {
    public:
        ~EventManager() = default;

        EventManager(const EventManager& other) = delete;
        EventManager& operator=(const EventManager& other) = delete;

        EventManager(EventManager && other) = delete;
        EventManager& operator=(EventManager && other) = delete;

    public:
        // Singleton accessor
        static EventManager& get();

        void push(Event && event);

        void execute();
        void executeAsync();

        uint32_t subscribe(Event && event);
        bool unsubscribe(uint32_t id);
        void unsubscribeAll(const std::string& eventName);

        void emit(const std::string& eventName);
        void emitAsync(const std::string& eventName);

    private:
        EventManager(size_t numThreads = DEFAULT_EVENT_THREADS);

    private:
        // Queue-based system
        std::priority_queue<Event> m_queue;
        std::mutex m_eventQueueMutex;

        // Listener-based system
        std::unordered_map<std::string, std::vector<EventListener>> m_listeners;
        std::unordered_map<uint32_t, std::string> m_listenerIdToName;
        std::mutex m_listenerMutex;

        ThreadPool m_threadPool;
};