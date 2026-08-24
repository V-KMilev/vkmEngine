#pragma once

#include "core/reflect.h"

namespace Vkm::Engine {

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
     */
    float volume = 1.0f;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::AudioListener)
    VKM_F(active),
    VKM_F(volume)
VKM_REFLECT_END()
