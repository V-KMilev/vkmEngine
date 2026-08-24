#pragma once

#include <string>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief A rig's playback head crossed one of its clip's markers this frame.
 *
 * SkeletalAnimationSystem enqueues this on the EventBus, so it is delivered at
 * the top of the next Simulation stage like a contact or a UI click, and a
 * Behavior handles it the same way:
 *
 *   subscribe<AnimationEvent>([this](const AnimationEvent& e) {
 *       if (e.entity == m_player && e.marker == "footstep") playFootstep();
 *   });
 *
 * Fired once per crossing, in either direction, and only for the clip the
 * Animator is playing - a crossfade's outgoing clip announces nothing, so a
 * blend cannot double a footstep.
 */
struct AnimationEvent {
    EntityId    entity;  ///< The entity carrying the Animator that crossed it.
    std::string marker;  ///< The crossed ClipMarker's name.
};

} // namespace Vkm::Engine
