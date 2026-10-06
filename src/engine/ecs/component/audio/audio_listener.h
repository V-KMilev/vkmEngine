#pragma once

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief The ear: the pose every spatial source is heard relative to.
 *
 * One is active at a time: the lowest-slot enabled one with a Transform; not tied to the camera.
 * With none, spatial sources stay silent while 2D ones play on.
 */
struct AudioListener {
    bool active = true;

    /**
     * @brief Master gain for everything this listener hears.
     *
     * A linear gain, not a perceptual slider position: drive it with position^2 or a
     * decibel curve. A negative or non-finite value is heard as silence, since an
     * infinite master gain would take the whole mix non-finite.
     */
    float volume = 1.0f;
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
    VKM_F(active)
    VKM_F(volume)
VKM_REFLECT_END()
