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
    if (component) {
        m_components[component->getType()] = std::move(component);
    }
    LOG_TRACE("Constructed Entity #%d type: '%s'", m_id, enumToString(m_type));
}

Entity::Entity(
    uint32_t id,
    EntityType type,
    std::vector<std::shared_ptr<Component>> && components
) : m_id(id),
    m_type(type) {
    for (auto&& component : components) {
        if (component) {
            m_components[component->getType()] = std::move(component);
        }
    }
    LOG_TRACE("Constructed Entity #%d type: '%s'", m_id, enumToString(m_type));
}

Entity::~Entity() {
    m_components.clear();
    LOG_TRACE("Destructed Entity #%d type: '%s'", m_id, enumToString(m_type));
}

uint32_t Entity::getID() const { return m_id; }
EntityType Entity::getType() const { return m_type; }

std::shared_ptr<Component> Entity::getComponent(ComponentType type) const {
    auto it = m_components.find(type);
    if (it != m_components.end()) {
        return it->second;
    }
    return nullptr;
}

bool Entity::hasComponent(ComponentType type) const {
    return m_components.find(type) != m_components.end();
}

void Entity::addComponent(std::shared_ptr<Component> && component) {
    if (!component) {
        return;
    }

    ComponentType type = component->getType();
    m_components[type] = std::move(component);
}
