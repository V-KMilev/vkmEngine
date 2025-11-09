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

constexpr const char* toString(EventPriority priority) {
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
            EventCallback callback,
            EventPriority priority,
            const std::string& name
        );
        
        bool operator<(const Event& other) const {
            return static_cast<int>(m_priority) < static_cast<int>(other.m_priority);
        }

    public:
        void execute() const;

        const EventCallback& getCallback() const;
        EventPriority getPriority() const;
        const std::string& getName() const;

    private:
        EventCallback m_callback;
        EventPriority m_priority;
        std::string m_name;
};