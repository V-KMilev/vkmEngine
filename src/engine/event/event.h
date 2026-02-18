#pragma once

#include <functional>
#include <string>

/**
 * @brief Type alias for an event callback function. 
 * The callback takes no arguments and returns void.
 */
using EventCallback = std::function<void()>;

/**
 * @enum EventPriority
 * @brief Specifies the priority of an event.
 *
 * Events may be dispatched in order of priority. 
 * IMMEDIATE events are processed as soon as possible.
 */
enum class EventPriority : int {
    LOW       = 1,      ///< Lowest event priority.
    MEDIUM    = 2,      ///< Medium event priority.
    HIGH      = 3,      ///< High event priority.
    IMMEDIATE = 4       ///< Event executes immediately.
};

/**
 * @brief Convert an EventPriority value to its string representation.
 * @param priority The EventPriority value.
 * @return const char* String name of the priority.
 */
constexpr const char* toString(EventPriority priority) {
    switch (priority) {
        case EventPriority::LOW:       return "LOW";
        case EventPriority::MEDIUM:    return "MEDIUM";
        case EventPriority::HIGH:      return "HIGH";
        case EventPriority::IMMEDIATE: return "IMMEDIATE";
        default: return "UNKNOWN";
    }
}

/**
 * @class Event
 * @brief Represents an event, encapsulating a callback, priority, and a unique name.
 *
 * Events are the fundamental units in the event system. Each event carries a callable,
 * a priority (for scheduling/execution order), and a name for identification
 * or subscription/dispatch via the EventSystem.
 */
class Event {
    public:
        Event() = delete;
        ~Event();

        Event(const Event& other) = delete;
        Event& operator=(const Event& other) = delete;

        Event(Event && other) noexcept = default;
        Event& operator=(Event && other) noexcept = default;

        /**
         * @brief Construct an event with a callback, priority, and name.
         * 
         * @param callback The function to execute when the event fires.
         * @param priority The event's priority for execution/scheduling.
         * @param name The unique name of the event, for pub/sub or queueing.
         */
        Event(
            EventCallback callback,
            EventPriority priority,
            const std::string& name
        );

        /**
         * @brief Compare events by their priority (for priority queues).
         * @param other The event to compare against.
         * @return True if this event's priority is less than the other's.
         */
        bool operator<(const Event& other) const {
            return static_cast<int>(m_priority) < static_cast<int>(other.m_priority);
        }

    public:
        /**
         * @brief Execute this event's callback.
         */
        void execute() const;

        /**
         * @brief Get the underlying callback function.
         * @return const reference to the callback.
         */
        const EventCallback& getCallback() const;

        /**
         * @brief Get this event's priority.
         * @return EventPriority value.
         */
        EventPriority getPriority() const;

        /**
         * @brief Get the unique name of this event.
         * @return const reference to the event's name string.
         */
        const std::string& getName() const;

    private:
        EventCallback m_callback;
        EventPriority m_priority;
        std::string m_name;
};