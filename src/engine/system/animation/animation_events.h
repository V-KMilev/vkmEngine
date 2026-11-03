#pragma once

#include <string>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief A rig's playback head crossed one of its clip's markers this tick.
 *
 * Enqueued on the EventBus by SkeletalAnimationSystem and heard by subscribing to it. When a marker
 * counts as crossed: docs/reference/animation.md, "Animation events".
 */
struct AnimationEvent {
    EntityId    entity;  ///< The entity carrying the Animator.
    std::string marker;  ///< The crossed ClipMarker's name.
};

} // namespace Vkm::Engine
