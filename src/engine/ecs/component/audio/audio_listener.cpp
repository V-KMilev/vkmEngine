#include "ecs/component/audio/audio_listener.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findActiveListener(const Scene& scene) {
    EntityId active{};
    scene.forEach<AudioListener, Transform>([&](EntityId id, const AudioListener& listener,
                                                const Transform&) {
        if (active || !listener.active) return;
        active = id;
    });
    return active;
}

} // namespace Vkm::Engine
