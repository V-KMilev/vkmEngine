#pragma once

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief Observer notified by a Scene when an entity is destroyed.
 *
 * Registered via Scene::addObserver() (and removeObserver()). onEntityDestroyed
 * fires at the start of Scene::destroyEntity - while the entity and its
 * components are still intact - on every destroy path, so a system can react to
 * deletions (BehaviorSystem fires script onDestroy this way) without Scene
 * depending on that system.
 *
 * The interface lives in the ecs layer; implementers live wherever they like
 * (e.g. BehaviorSystem in system/script), which keeps Scene a pure registry
 * with no dependency back on the systems that observe it.
 */
struct ISceneObserver {
    virtual ~ISceneObserver() = default;

    /**
     * @brief Called just before @p id's components are removed.
     * @param id The entity being destroyed; still alive, components intact.
     */
    virtual void onEntityDestroyed(EntityId id) = 0;
};

} // namespace Engine
