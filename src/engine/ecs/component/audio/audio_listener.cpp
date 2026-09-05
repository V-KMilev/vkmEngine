#include "ecs/component/audio/audio_listener.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findActiveListener(const Scene& scene) {
    return findLowestSlot<AudioListener, Transform>(scene,
        [](const AudioListener& listener, const Transform&) { return listener.active; });
}

} // namespace Vkm::Engine
