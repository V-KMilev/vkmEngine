#include "event/event.h"

#include "logger.h"
#include "debug/print_helper.h"

Event::Event(
    EventCallback callback,
    EventPriority priority,
    const std::string& name
) : m_callback(std::move(callback)),
    m_priority(priority),
    m_name(name)
{
    LOG_TRACE("Constructed Event '%s', priority: '%s'", m_name.c_str(), enumToString(m_priority));
}

Event::~Event() {
    LOG_TRACE("Destructed Event '%s', priority: '%s'", m_name.c_str(), enumToString(m_priority));
}

void Event::execute() const {
    if (!m_callback) {
        LOG_WARNING("[EVENT] No callback found for event '%s' - '%s'", m_name.c_str(), enumToString(m_priority));
        return;
    }

    try {
        m_callback();
        LOG_VERBOSE("[EVENT] Executed '%s' - '%s'", m_name.c_str(), enumToString(m_priority));

    } catch (const std::exception& e) {
        LOG_FATAL("[EVENT] Exception in event '%s' (%s): %s", m_name.c_str(), enumToString(m_priority), e.what());
    } catch (...) {
        LOG_FATAL("[EVENT] Unknown exception in event '%s' (%s)", m_name.c_str(), enumToString(m_priority));
    }
}

EventPriority Event::getPriority() const { return m_priority; }
const EventCallback& Event::getCallback() const { return m_callback; }
const std::string& Event::getName() const { return m_name; }