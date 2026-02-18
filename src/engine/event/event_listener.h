#pragma once

#include <cstdint>

#include "event/event.h"

/**
 * @class EventListener
 * @brief Wrapper for an event callback, associated with a unique listener ID, for named signals.
 *
 * Stores an Event (with callback, priority, and name), and listener ID for efficient management
 * by EventSystem. Events may be executed by emitting (firing) the corresponding signal.
 */
class EventListener {
    public:
        EventListener() = delete;
        ~EventListener();

        EventListener(const EventListener& other) = delete;
        EventListener& operator=(const EventListener& other) = delete;

        EventListener(EventListener && other) noexcept = default;
        EventListener& operator=(EventListener && other) noexcept = default;

        /**
         * @brief Construct a listener from an event with a specified listener ID.
         * @param event Event object (move).
         * @param id Listener ID.
         */
        explicit EventListener(Event && event, uint32_t id);

        /**
         * @brief Compare by event priority.
         * @param other Reference to another EventListener.
         * @return true if this listener's priority is less than other's.
         */
        bool operator<(const EventListener& other) const {
            return static_cast<int>(m_event.getPriority()) < static_cast<int>(other.m_event.getPriority());
        }

    public:
        /**
         * @brief Execute the listener's callback.
         */
        void execute() const;

        /**
         * @brief Get the unique listener ID.
         * @return uint32_t Listener ID.
         */
        uint32_t getID() const;

    private:
        Event m_event;
        uint32_t m_id;
};

