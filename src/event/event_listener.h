#pragma once

#include <functional>
#include <cstdint>

#include "event.h"

class EventListener {
    public:
        EventListener() = delete;
        ~EventListener();

        EventListener(const EventListener& other) = delete;
        EventListener& operator=(const EventListener& other) = delete;

        EventListener(EventListener && other) noexcept = default;
        EventListener& operator=(EventListener && other) noexcept = default;

        explicit EventListener(Event && event, uint32_t id);

        bool operator<(const EventListener& other) const {
            return static_cast<int>(m_event.getPriority()) < static_cast<int>(other.m_event.getPriority());
        }

    public:
        void execute() const;

        uint32_t getID() const;

    private:
        Event m_event;
        uint32_t m_id;
};

