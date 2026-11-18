#include "undo/component_commands.h"

#include <type_traits>

#include "ecs/scene.h"
#include "undo/command_host.h"
#include "undo/composite_command.h"
#include "undo/component_edit.h"
#include "undo/entity_snapshot.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {

template <typename T>
T& sceneValue(Scene& scene) {
    if constexpr (std::is_same_v<T, Environment>) {
        return scene.environment();
    } else {
        static_assert(std::is_same_v<T, PhysicsSettings>, "SceneValueEditCommand: no Scene accessor for T");
        return scene.physics();
    }
}

} // namespace

template <typename T>
void SceneValueEditCommand<T>::redo(Scene& scene, CommandHost&) {
    sceneValue<T>(scene) = m_after;
}

template <typename T>
void SceneValueEditCommand<T>::undo(Scene& scene, CommandHost&) {
    sceneValue<T>(scene) = m_before;
}

template <typename T>
bool SceneValueEditCommand<T>::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<SceneValueEditCommand<T>*>(&incoming);
    if (!p) return false;
    m_after = p->m_after;
    m_label = p->m_label;
    return true;
}

template class SceneValueEditCommand<Environment>;
template class SceneValueEditCommand<PhysicsSettings>;

template <typename T>
void AddComponentCommand<T>::redo(Scene& scene, CommandHost& host) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || scene.has<T>(e)) return;
    T copy = m_value;
    scene.add(e, std::move(copy));
    followReturns(scene, e, host.commandStack());
}

template <typename T>
void AddComponentCommand<T>::undo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<T>(e)) return;
    scene.remove<T>(e);
}

template <typename T>
void RemoveComponentCommand<T>::redo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<T>(e)) return;
    scene.remove<T>(e);
}

template <typename T>
void RemoveComponentCommand<T>::undo(Scene& scene, CommandHost& host) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || scene.has<T>(e)) return;
    T copy = m_snapshot;
    scene.add(e, std::move(copy));
    followReturns(scene, e, host.commandStack());
}

template <typename T>
void ComponentEditCommand<T>::redo(Scene& scene, CommandHost& host) {
    const EntityId e = liveEntity(scene, m_entity);
    T* held = scene.tryGet<T>(e);
    if (!held) return;
    *held = m_after;
    followReturns(scene, e, host.commandStack());
}

template <typename T>
void ComponentEditCommand<T>::undo(Scene& scene, CommandHost& host) {
    const EntityId e = liveEntity(scene, m_entity);
    T* held = scene.tryGet<T>(e);
    if (!held) return;
    *held = m_before;
    followReturns(scene, e, host.commandStack());
}

template <typename T>
bool ComponentEditCommand<T>::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<ComponentEditCommand<T>*>(&incoming);
    if (!p || p->m_entity != m_entity) return false;
    m_after = p->m_after;
    return true;
}

#define VKM_EDITOR_INSTANTIATE_COMMAND(Type, Key) \
    template class AddComponentCommand<Type>;     \
    template class RemoveComponentCommand<Type>;  \
    template class ComponentEditCommand<Type>;
VKM_SCENE_VALUE_COMPONENTS(
    VKM_EDITOR_INSTANTIATE_COMMAND,
    VKM_EDITOR_INSTANTIATE_COMMAND,
    VKM_EDITOR_INSTANTIATE_COMMAND
)
#undef VKM_EDITOR_INSTANTIATE_COMMAND

std::string ScriptEditCommand::capture(const Scene& scene, EntityId id) {
    const ScriptComponent* script = scene.tryGet<ScriptComponent>(id);
    return script ? scriptJsonOf(*script) : std::string();
}

void ScriptEditCommand::restore(Scene& scene, EntityId id, const std::string& json) {
    // Removed first: SparseSet::add refuses a key it holds, so the list already
    // there would stay and the restored one be dropped silently.
    if (scene.has<ScriptComponent>(id)) scene.remove<ScriptComponent>(id);
    if (!json.empty()) addScriptFromJson(scene, id, json);
}

void ScriptEditCommand::redo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e) return;
    restore(scene, e, m_after);
}

void ScriptEditCommand::undo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e) return;
    restore(scene, e, m_before);
}

bool ScriptEditCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<ScriptEditCommand*>(&incoming);
    if (!p || p->m_entity != m_entity) return false;
    m_after = p->m_after;
    m_label = p->m_label;
    return true;
}

void pushActiveCamera(Scene& scene, ResourceManager& resources, CommandHost& host, EntityId target) {
    // Gathered first: making an override step writes the scene, which must not
    // happen inside its own walk.
    std::vector<EntityId> moving;
    scene.forEach<Camera>([&](EntityId id, const Camera& c) {
        if (c.active != (id == target)) moving.push_back(id);
    });
    if (moving.empty()) return;

    auto step = std::make_unique<CompositeCommand>("Set Main Camera");
    for (const EntityId id : moving) {
        Camera& camera = scene.get<Camera>(id);
        const Camera before = camera;
        camera.active = id == target;
        step->add(editStep<Camera>(scene, resources, id, before, camera, "Set Main Camera"));
    }
    host.pushStep(std::move(step));
}

} // namespace Vkm::Engine
