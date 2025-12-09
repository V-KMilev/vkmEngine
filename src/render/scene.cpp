#include "scene.h"

namespace Engine {

Entity& Scene::createEntity(EntityType type) {
    m_entities.emplace_back(m_entityId++, type);
    return m_entities.back();
}

/// Create an entity with one component.
Entity& Scene::createEntity(EntityType type, std::shared_ptr<Component>&& component) {
    m_entities.emplace_back(m_entityId++, type, std::move(component));
    return m_entities.back();
}

/// Create an entity with many components.
Entity& Scene::createEntity(EntityType type, std::vector<std::shared_ptr<Component>>&& components) {
    m_entities.emplace_back(m_entityId++, type, std::move(components));
    return m_entities.back();

}

} // namespace Engine