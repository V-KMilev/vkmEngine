#pragma once

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief The ear: the pose every spatial source is heard relative to.
 *
 * Exactly one listener is active at a time - the first entity carrying an
 * enabled AudioListener and a Transform, ties broken by storage order, which is
 * the rule findActiveCamera already states for the eye. Deliberately not tied
 * to the camera: a third-person game hears from its character while looking
 * from an orbit rig, and a cutscene camera should not move the ear. Two cameras
 * is therefore not a question audio has to answer; two listeners is, and the
 * answer is that the second one is ignored.
 *
 * With no active listener there is nothing for a distance to be measured from,
 * so spatial sources stay silent while 2D ones (music, UI) play on. AudioSystem
 * says so once rather than every frame.
 */
struct AudioListener {
    bool active = true;

    /**
     * @brief Master gain for everything this listener hears.
     *
     * Lives here rather than in the Environment because it is a property of who
     * is listening, not of the world: a volume slider writes it, and it goes
     * away with the listener rather than outliving it in the scene settings.
     *
     * A LINEAR GAIN, not a slider position - it is multiplied into the mix as
     * written, so 0.5 is half the amplitude and roughly two thirds as loud, not
     * half as loud. A player-facing slider is perceptual and owes the
     * conversion: drive this with position^2 or a decibel curve, or most of the
     * useful range hides in the slider's top quarter.
     *
     * A negative or non-finite value is heard as silence rather than as itself,
     * because an infinite master gain would take the entire mix non-finite -
     * every sound in the game, not just this one.
     */
    float volume = 1.0f;
};

/**
 * @brief The scene's active listener: the first entity whose AudioListener::active is set.
 *
 * The one definition of a rule everything that answers "which listener" has to
 * agree on - AudioSystem places the ear through it, the Inspector uses it to
 * say which of two listeners is the one being heard from and to warn a
 * positioned source that there is no ear at all, and the viewport draws every
 * listener icon but that one dim. A Transform is required as well as an
 * AudioListener, because a listener with no pose has nowhere to measure a
 * distance from. Two active listeners are broken by lowest slot (see
 * findLowestSlot); an empty result means the scene has no ear, which is a
 * normal state rather than an error - spatial sources go silent and 2D ones
 * play on.
 *
 * Deliberately without findActiveCamera's cached-hint parameter: that exists
 * because two systems each keep a cached camera entity, and nothing on the
 * audio side caches one.
 *
 * @param scene The scene to search.
 * @return The active listener entity, or {} when there is none.
 */
EntityId findActiveListener(const Scene& scene);

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::AudioListener)
    VKM_F(active),
    VKM_F(volume)
VKM_REFLECT_END()
