#include "scene.h"

namespace Engine {

Scene::Scene() : m_entityId(1), m_componentId(1) {}

std::shared_ptr<Entity> Scene::createEntity(EntityType type) {
    auto entity = std::make_shared<Entity>(m_entityId++, type);
    m_entities.emplace_back(entity);
    return entity;
}

std::shared_ptr<Entity> Scene::createEntity(EntityType type, std::shared_ptr<Component> && component) {
    auto entity = std::make_shared<Entity>(m_entityId++, type, std::move(component));
    m_entities.emplace_back(entity);
    return entity;
}

std::shared_ptr<Entity> Scene::createEntity(EntityType type, std::vector<std::shared_ptr<Component>> && components) {
    auto entity = std::make_shared<Entity>(m_entityId++, type, std::move(components));
    m_entities.emplace_back(entity);
    return entity;
}

std::shared_ptr<Component> Scene::findComponent(const Entity& entity, ComponentType type) {
    for (const auto& component : entity.getComponents()) {
        if (component && component->getType() == type) {
            return component;
        }
    }
    return nullptr;
}

} // namespace Engine