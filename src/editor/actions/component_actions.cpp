#include "actions/component_actions.h"

#include <memory>
#include <string>

#include "undo/component_commands.h"
#include "editor_state.h"
#include "undo/prefab_overrides.h"
#include "ecs/scene.h"
#include "system/script/script_component.h"
namespace Vkm::Engine {
namespace EditorActions {

void addBehavior(Scene& scene, EditorState& state, EntityId id, const std::string& type) {
    std::unique_ptr<BehaviorBase> behavior = BehaviorRegistry::get().create(type);
    if (!behavior) return;

    const bool had = scene.has<ScriptComponent>(id);
    const std::string before = had ? ScriptEditCommand::capture(scene, id) : std::string{};
    scene.getOrAdd<ScriptComponent>(id).behaviors.push_back(std::move(behavior));
    const std::string after = ScriptEditCommand::capture(scene, id);
    state.pushStep(std::make_unique<ScriptEditCommand>(id, before, after, "Add Behavior"));
    if (!had) {
        PrefabOverrides::warnComponentIsPrefabs(scene, state, id, "Script", "is not stored in the scene");
    }
}

} // namespace EditorActions
} // namespace Vkm::Engine
