#include "actions/entity_copy.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "undo/entity_commands.h"
#include "undo/entity_snapshot.h"
#include "editor_state.h"
#include "ecs/scene.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"
#include "resource/resource_manager.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/physics/ragdoll.h"
namespace Vkm::Engine {
namespace EditorActions {

namespace {

/**
 * @brief Which nodes of @p snap lie under a prefab instance's root inside it: a group copied with
 *        an instance parented under it.
 *
 * Such a node keeps its PrefabEntity uid for that instance's overrides; any other inherited uid
 * would let the original's instance patch the clone. Relies on parents listed before children.
 *
 * @param snap The subtree being copied, root first.
 * @return One flag per node, true for a node under an instance root in the copy.
 */
std::vector<bool> nodesUnderInstanceRoot(const SubtreeSnapshot& snap) {
    std::vector<bool> under(snap.nodes.size(), false);
    for (size_t i = 1; i < snap.nodes.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (snap.nodes[j].snap.slotIndex != snap.nodes[i].parentSlot) continue;
            under[i] = under[j] || snap.nodes[j].snap.get<PrefabInstance>().has_value();
            break;
        }
    }
    return under;
}

} // namespace

Transform worldPoseOf(const Scene& scene, EntityId id) {
    if (!scene.has<Transform>(id)) return Transform{};
    return Transform::fromModelMatrix(HierarchyOperations::computeWorldMatrix(scene, id));
}

void readyCopy(SubtreeSnapshot& snap) {
    const std::vector<bool> under = nodesUnderInstanceRoot(snap);
    for (size_t i = 0; i < snap.nodes.size(); ++i) {
        EntitySnapshot& node = snap.nodes[i].snap;
        if (auto& camera = node.get<Camera>())       camera->active = false;
        if (auto& animation = node.get<Animation>()) animation->playing = false;
        // Its bones are cloned and re-stamped by apply, so it can still be thrown.
        if (auto& ragdoll = node.get<Ragdoll>()) ragdoll->active = false;
        if (!under[i]) node.get<PrefabEntity>().reset();
    }
}

Duplicate placeInstance(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    const PrefabInstance& instance,
    const Transform& at,
    EntityId parent,
    const char* label
) {
    const EntityId copy = scene.createEntity();
    scene.add(copy, Transform{at});
    scene.add(copy, PrefabInstance{instance});
    if (!Prefab::instantiateInto(scene, resources, instance.source, copy, instance.overrides)) {
        // The subtree: a build that stopped partway has parented what it made.
        HierarchyOperations::destroyHierarchy(scene, copy);
        const std::string name = std::filesystem::path(instance.source).filename().string();
        state.toasts.push(ToastKind::Error, "Could not build the instance of '" + name + "'");
        return {};
    }
    if (parent) HierarchyOperations::setParent(scene, copy, parent);
    const uint32_t under = parent.slot();
    return {copy, std::make_unique<PlacePrefabCommand>(resources, instance, copy, at, label, under)};
}

} // namespace EditorActions
} // namespace Vkm::Engine
