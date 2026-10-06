#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior.h"

#include <utility>

#include "logger.h"

#include "system/script/script_component.h"

namespace Vkm::Engine {

const Behavior* findBehaviorNamed(const Scene& scene, EntityId entity, std::string_view typeName) {
    const ScriptComponent* script = scene.tryGet<ScriptComponent>(entity);
    if (!script) return nullptr;

    for (const std::unique_ptr<Behavior>& behavior : script->behaviors) {
        if (behavior && typeName == behavior->typeName()) return behavior.get();
    }
    return nullptr;
}

Behavior* findBehaviorNamed(Scene& scene, EntityId entity, std::string_view typeName) {
    // The scene's own, so the behavior is as writable as the scene handed in.
    return const_cast<Behavior*>(findBehaviorNamed(std::as_const(scene), entity, typeName));
}

void Behavior::warnSpawnOnClient() {
    static bool s_warned = false;
    if (s_warned) return;
    s_warned = true;
    LOG_WARNING(
        "A behavior spawned an entity on a connected client. It exists "
        "here alone, and its slot is one the server may give to "
        "something it spawns; see networking.md, \"Identity is the "
        "scene slot\""
    );
}

} // namespace Vkm::Engine
