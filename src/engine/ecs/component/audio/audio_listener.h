#pragma once

#include "base/reflect/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief The ear: the pose every spatial source is heard relative to.
 *
 * One is heard at a time: the lowest-slot one with active set and a Transform; not tied to the camera.
 * With none, the active camera hears, at unity gain (AudioSystem::updateListener).
 */
struct AudioListener {
    bool active = true;

    float volume = 1.0f;  ///< Master gain for everything this listener hears: linear, not a slider's
                          ///< position, so drive it with position^2 or a decibel curve. A negative or
                          ///< non-finite value is heard as silence, as an infinite master gain would take the
                          ///< whole mix non-finite.
};

/**
 * @brief The scene's active listener: the lowest-slot entity whose AudioListener::active is set.
 *
 * A Transform is required too. Ties break by lowest slot (see findLowestSlot).
 *
 * @param scene Scene to search.
 * @return The active listener, or {} when the scene has none (a normal state).
 */
EntityId findActiveListener(const Scene& scene);

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::AudioListener)
    VKM_F(active, "One listener is heard: of several, the lowest-numbered entity")
    VKM_F(volume, 0.0f, 1.0f, "Master gain for everything it hears")
VKM_REFLECT_END()
