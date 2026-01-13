#include "event_listener.h"

#include "logger.h"
#include "print_helper.h"

EventListener::EventListener(Event && event, uint32_t id)
    : m_event(std::move(event)),
      m_id(id)
{
    if (!m_event.getCallback()) {
        LOG_WARNING("[EVENT LISTENER] Created listener ID:%u for '%s' with no callback", m_id, m_event.getName().c_str());
    }

    LOG_TRACE("Created listener ID:%u for '%s'", m_id, m_event.getName().c_str());
}

EventListener::~EventListener() {
    LOG_TRACE("Destructed listener ID:%u for '%s'", m_id, m_event.getName().c_str());
}

void EventListener::execute() const {
    m_event.execute();
}

uint32_t EventListener::getID() const {
    return m_id;
}
