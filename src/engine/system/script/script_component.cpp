#include "system/script/script_component.h"

namespace Vkm::Engine {

EntityId findEntityWithBehaviorNamed(Scene& scene, std::string_view typeName) {
    EntityId found{};
    scene.forEach<ScriptComponent>([&](EntityId entity, ScriptComponent& script) {
        if (found && found.slot() < entity.slot()) return;
        if (findBehaviorNamed(scene, entity, typeName)) found = entity;
    });
    return found;
}

void reportUnregisteredBehavior(const char* typeName) {
    reportError(
        "Behavior",
        typeName,
        "added in code but never registered: it runs, but a saved scene cannot load it "
        "and a script reload drops it. Register it in vkmRegisterBehaviors."
    );
}

} // namespace Vkm::Engine
