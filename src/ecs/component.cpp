#include "component.h"

#include "logger.h"
#include "print_helper.h"

Component::Component(
    uint32_t id,
    ComponentType type
) : m_id(id),
    m_type(type)
{
    LOG_TRACE("Constructed Component #%d type: '%s'", m_id, enumToString(m_type));
}

Component::~Component() {
    LOG_TRACE("Destructed Component #%d type: '%s'", m_id, enumToString(m_type));
}

uint32_t Component::getID() const { return m_id; }
ComponentType Component::getType() const { return m_type; }
