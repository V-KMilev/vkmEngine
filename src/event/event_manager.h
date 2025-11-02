#pragma once

#include <queue>
#include <mutex>

#include "event.h"
#include "thread_pool.h"

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

    private:
        EventManager(size_t numThreads = DEFAULT_EVENT_THREADS);

        void executeEvent(const Event& event) const;

    private:
        std::priority_queue<Event> m_queue;
        std::mutex m_mutex;

        ThreadPool m_threadPool;
};