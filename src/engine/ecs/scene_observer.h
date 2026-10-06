#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief Observer notified by a Scene when an entity is destroyed.
 *
 * Registered via Scene::addObserver(). Lets a system react to deletions without
 * Scene depending on it. Scene::clear() and Scene::swap() do not notify.
 */
class ISceneObserver {
    public:
        ISceneObserver() = default;
        virtual ~ISceneObserver() = default;

        ISceneObserver(const ISceneObserver& other) = delete;
        ISceneObserver& operator=(const ISceneObserver& other) = delete;

        ISceneObserver(ISceneObserver && other) = delete;
        ISceneObserver& operator=(ISceneObserver && other) = delete;

    public:
        /**
         * @brief Called just before @p id's components are removed.
         * @param id Entity being destroyed; still alive, components intact.
         */
        virtual void onEntityDestroyed(EntityId id) = 0;
};

} // namespace Vkm::Engine
