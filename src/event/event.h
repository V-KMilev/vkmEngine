#pragma once

#include <functional>
#include <string>

using EventCallback = std::function<void()>;

enum class EventPriority : int {
    LOW       = 1,
    MEDIUM    = 2,
    HIGH      = 3,
    IMMEDIATE = 4
};

inline const char* toString(EventPriority priority) {
    switch (priority) {
        case EventPriority::LOW:       return "LOW";
        case EventPriority::MEDIUM:    return "MEDIUM";
        case EventPriority::HIGH:      return "HIGH";
        case EventPriority::IMMEDIATE: return "IMMEDIATE";
        default: return "UNKNOWN";
    }
}

class Event {
    public:
        Event() = delete;
        ~Event();

        Event(const Event& other) = delete;
        Event& operator=(const Event& other) = delete;

        Event(Event && other) noexcept = default;
        Event& operator=(Event && other) noexcept = default;

        Event(
            EventPriority priority,
            EventCallback callback,
            const std::string& name
        );

        bool operator<(const Event& other) const {
            return static_cast<int>(m_priority) < static_cast<int>(other.m_priority);
        }

    public:
        EventPriority getPriority() const;
        const EventCallback& getCallback() const;
        const std::string& getName() const;

    private:
        EventPriority m_priority;
        EventCallback m_callback;
        std::string m_name;
};