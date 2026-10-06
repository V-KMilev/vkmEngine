#pragma once

#include <string>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief A rig's playback head crossed one of its clip's markers this tick.
 *
 * Enqueued on the EventBus by SkeletalAnimationSystem; a Behavior subscribes:
 *
 *   subscribe<AnimationEvent>([this](const AnimationEvent& e) {
 *       if (e.entity == m_player && e.marker == "footstep") playFootstep();
 *   });
 *
 * Once per crossing, either direction, and only for the clip the Animator is playing, so a crossfade
 * cannot double a footstep.
 */
struct AnimationEvent {
    EntityId    entity;  ///< The entity carrying the Animator.
    std::string marker;  ///< The crossed ClipMarker's name.
};

} // namespace Vkm::Engine
