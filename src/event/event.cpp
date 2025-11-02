#include "event.h"

#include "logger.h"
#include "print_helper.h"

Event::Event(
    EventPriority priority,
    EventCallback callback,
    const std::string& name
) : m_priority(priority),
    m_callback(std::move(callback)),
    m_name(name)
{
    LOG_TRACE("Constructed Event '%s', priority: '%s'", m_name.c_str(), enumToString(m_priority));
}

Event::~Event() {
    LOG_TRACE("Destructed Event '%s', priority: '%s'", m_name.c_str(), enumToString(m_priority));
}

EventPriority Event::getPriority() const { return m_priority; }
const EventCallback& Event::getCallback() const { return m_callback; }
const std::string& Event::getName() const { return m_name; }
