#include "entity.h"

#include "logger.h"
#include "print_helper.h"

#include "component.h"

Entity::Entity(
    uint32_t id,
    EntityType type
) : m_id(id),
    m_type(type),
    m_components() {
    LOG_TRACE("Constructed Entity #%d type: '%s'", m_id, enumToString(m_type));
}

Entity::Entity(
    uint32_t id,
    EntityType type,
    std::shared_ptr<Component> && component
) : m_id(id),
    m_type(type) {
    m_components.reserve(1);
    m_components.push_back(std::move(component));

    LOG_TRACE("Constructed Entity #%d tpye: '%s'", m_id, enumToString(m_type));
}

Entity::Entity(
    uint32_t id,
    EntityType type,
    std::vector<std::shared_ptr<Component>> && components
) : m_id(id),
    m_type(type),
    m_components(std::move(components)) {
    LOG_TRACE("Constructed Entity #%d type: '%s'", m_id, enumToString(m_type));
}

Entity::~Entity() {
    m_components.clear();
    LOG_TRACE("Destructed Entity #%d type: '%s'", m_id, enumToString(m_type));
}

uint32_t Entity::getID() const { return m_id; }
EntityType Entity::getType() const { return m_type; }

std::vector<std::shared_ptr<Component>>& Entity::getComponents() { return m_components; }
const std::vector<std::shared_ptr<Component>>& Entity::getComponents() const { return m_components; }

void Entity::addComponent(std::shared_ptr<Component> && component) {
    m_components.push_back(std::move(component));
}
