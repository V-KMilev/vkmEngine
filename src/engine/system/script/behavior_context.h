#pragma once

#include <string>
#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {

struct FrameContext;

/**
 * @brief An entity BehaviorBase::destroyAfter destroys when its seconds have run out on live ticks.
 */
struct TimedDestroy {
    EntityId entity;
    float    seconds = 0.0f;
};

/**
 * @brief What a behavior reaches: the session's frame, and what it asks BehaviorSystem to do after
 *        the hook pass.
 *
 * Owned by BehaviorSystem for the session, so a subscribe() lambda may keep using it.
 */
struct BehaviorContext {
    FrameContext*             frame          = nullptr;  ///< Set by BehaviorSystem::init.
    std::vector<EntityId>     pendingDestroy;            ///< destroy()'s, drained after each pass.
    std::vector<TimedDestroy> timedDestroy;              ///< destroyAfter()'s, counted down on live ticks.
    std::string               pendingSceneLoad;          ///< loadScene()'s; empty when none.
    bool                      pendingRestart = false;    ///< restartScene() asked.
    bool                      ticking        = false;    ///< Inside BehaviorSystem's fixed-tick pass.
    bool                      covered        = false;    ///< The splash or the loading screen showed over
                                                         ///< the world as BehaviorSystem::update last ran.
};

} // namespace Vkm::Engine
