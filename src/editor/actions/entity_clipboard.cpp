#include "actions/entity_clipboard.h"

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "io/scene/scene_serializer.h"
#include "undo/entity_commands.h"
#include "undo/entity_snapshot.h"
#include "undo/composite_command.h"
#include "editor_state.h"
#include "actions/copy_parent.h"
#include "ecs/scene.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "actions/entity_copy.h"
#include "actions/edit_actions.h"
namespace Vkm::Engine {
namespace EditorActions {

void copySelection(const Scene& scene, const ResourceManager& resources, EditorState& state) {
    // The copies, each a root at its world pose, written as a scene is.
    std::vector<SubtreeSnapshot> trees;
    std::vector<EntityId>        treeParents;
    std::vector<EntityId>        instances;
    for (EntityId id : state.selection.entities()) {
        if (!scene.isAlive(id) || hasSelectedAncestor(scene, state, id)) continue;
        if (scene.has<PrefabInstance>(id)) {
            instances.push_back(id);
            continue;
        }
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        if (snap.nodes.empty()) continue;
        readyCopy(snap);
        if (auto& pose = snap.nodes.front().snap.get<Transform>()) pose = worldPoseOf(scene, id);
        trees.push_back(std::move(snap));
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        treeParents.push_back(node ? node->parent : EntityId{});
    }

    Scene taken;
    auto clip = std::make_shared<EntityClipboard>();
    clip->world = scene.epoch();
    // Together, so what one copy names of another is kept between them.
    const std::vector<EntityId> roots = SubtreeSnapshot::applyTogether(taken, trees);
    for (size_t i = 0; i < roots.size(); ++i) {
        if (roots[i]) clip->parents.emplace_back(roots[i].slot(), treeParents[i]);
    }
    for (EntityId id : instances) {
        // A reference, as a scene stores one; the paste builds it from its file.
        const EntityId root = taken.createEntity();
        taken.add(root, Transform{worldPoseOf(scene, id)});
        taken.add(root, PrefabInstance{scene.get<PrefabInstance>(id)});
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        clip->parents.emplace_back(root.slot(), node ? node->parent : EntityId{});
    }
    if (clip->parents.empty()) return;
    clip->scene           = SceneSerializer::saveToString(taken, resources);
    state.entityClipboard = std::move(clip);
}

void cutSelection(Scene& scene, const ResourceManager& resources, EditorState& state) {
    copySelection(scene, resources, state);
    deleteSelection(scene, state);
}

void paste(Scene& scene, ResourceManager& resources, EditorState& state) {
    // Its own hold: nothing below may drop it mid-paste.
    const std::shared_ptr<const EntityClipboard> clip = state.entityClipboard;
    if (!clip || clip->parents.empty()) return;
    Scene taken;
    if (!SceneSerializer::loadFromString(clip->scene, taken, resources)) {
        state.toasts.push(ToastKind::Error, "Could not paste");
        return;
    }

    // Where each lands: under the same parent while copyParent keeps it, else in the world.
    const bool sameWorld = clip->world == scene.epoch();
    struct Landing {
        EntityId  copy;    ///< In `taken`.
        EntityId  parent;  ///< In the scene; null for the root.
        Transform at;      ///< Its pose under that parent.
    };
    std::vector<Landing> landings;
    for (const auto& [slot, wasUnder] : clip->parents) {
        const EntityId copy = taken.entityAt(slot);
        if (!taken.isAlive(copy)) continue;
        const EntityId parent = sameWorld ? copyParent(scene, wasUnder) : EntityId{};
        const Transform* pose = taken.tryGet<Transform>(copy);
        Transform at = pose ? *pose : Transform{};
        if (parent && scene.has<Transform>(parent)) {
            const glm::mat4 under = HierarchyOperations::computeWorldMatrix(scene, parent);
            at = Transform::fromModelMatrix(glm::inverse(under) * Transform::computeModelMatrix(at));
        }
        landings.push_back({copy, parent, at});
    }

    auto batch = std::make_unique<CompositeCommand>("Paste");
    std::vector<EntityId> pasted;
    const auto keep = [&](Duplicate placed) {
        if (!placed.entity) return;
        batch->add(std::move(placed.step));
        pasted.push_back(placed.entity);
    };
    // The subtrees together, so what one names of another is kept between their copies.
    std::vector<SubtreeSnapshot> trees;
    std::vector<const Landing*>  treeLandings;
    for (const Landing& landing : landings) {
        if (const PrefabInstance* held = taken.tryGet<PrefabInstance>(landing.copy)) {
            const PrefabInstance instance = *held;
            const EntityId under = landing.parent;
            keep(placeInstance(scene, resources, state, instance, landing.at, under, "Paste Entity"));
            continue;
        }
        SubtreeSnapshot snap = SubtreeSnapshot::capture(taken, landing.copy);
        if (snap.nodes.empty()) continue;
        if (auto& pose = snap.nodes.front().snap.get<Transform>()) pose = landing.at;
        trees.push_back(std::move(snap));
        treeLandings.push_back(&landing);
    }
    const std::vector<EntityId> made = SubtreeSnapshot::applyTogether(scene, trees);
    for (size_t i = 0; i < made.size(); ++i) {
        if (!made[i]) continue;
        const EntityId parent = treeLandings[i]->parent;
        if (parent) HierarchyOperations::setParent(scene, made[i], parent);
        SubtreeSnapshot placed = SubtreeSnapshot::capture(scene, made[i]);
        keep({made[i], std::make_unique<CreateSubtreeCommand>(std::move(placed), "Paste Entity")});
    }
    if (pasted.empty()) return;

    state.pushStep(std::move(batch));
    state.selectEntity(pasted.front());
    for (size_t i = 1; i < pasted.size(); ++i) state.selection.add(pasted[i]);
}

} // namespace EditorActions
} // namespace Vkm::Engine
