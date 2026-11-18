#define VKM_LOG_CATEGORY "EDITOR"

#include "undo/entity_commands.h"

#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "vkm_log/logger.h"

#include "ecs/scene.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/prefab_instances.h"
#include "io/scene/prefab.h"
#include "resource/resource_manager.h"
#include "undo/command_host.h"
#include "undo/composite_command.h"

namespace Vkm::Engine {

namespace {

void toastOtherEntity(CommandHost& host, const char* label) {
    host.pushToast(ToastKind::Error, std::string("'") + label + "' skipped: another entity holds its slot");
}

void refuseOtherEntity(CommandHost& host, const char* label, uint32_t slot) {
    LOG_ERROR("'%s' skipped: slot %u holds another entity", label, slot);
    toastOtherEntity(host, label);
}

// Each node whose slot holds an entity, about to be destroyed, keeps its id: a reference from outside
// still names it when apply brings the node back.
void keepDestroyedIds(const Scene& scene, SubtreeSnapshot& snap) {
    for (SubtreeSnapshot::Node& node : snap.nodes) {
        if (scene.isAliveAtIndex(node.snap.slotIndex)) node.entity = scene.entityAt(node.snap.slotIndex);
    }
}

// Destroys a captured subtree, refused when its root's slot holds another entity.
void destroyCaptured(Scene& scene, CommandHost& host, SubtreeSnapshot& snap, const char* label) {
    if (snap.nodes.empty()) return;
    const EntityId root = snap.liveRoot(scene);
    if (!root) {
        refuseOtherEntity(host, label, snap.nodes.front().snap.slotIndex);
        return;
    }
    keepDestroyedIds(scene, snap);
    HierarchyOperations::destroyHierarchy(scene, root);
}

} // namespace

void CreateEntityCommand::redo(Scene& scene, CommandHost& host) {
    const EntityId e = reclaimSlot(scene, m_snap.slotIndex);
    if (!e) {
        refuseOtherEntity(host, label(), m_snap.slotIndex);
        return;
    }
    m_snap.apply(scene, e);
    if (m_destroyed) host.commandStack().noteReturn(scene, m_destroyed, e);
    if (m_parentSlot && scene.isAliveAtIndex(m_parentSlot)) {
        const EntityId parent = scene.entityAt(m_parentSlot);
        HierarchyOperations::setParent(scene, e, parent);
    }
    host.selectEntity(e);
}

void CreateEntityCommand::undo(Scene& scene, CommandHost& host) {
    EntityId id = scene.entityAt(m_snap.slotIndex);
    if (!scene.isAlive(id)) return;
    if (!m_snap.describes(scene, id)) {
        refuseOtherEntity(host, label(), m_snap.slotIndex);
        return;
    }
    m_destroyed = id;
    scene.destroyEntity(id);
}

void SubtreeReplaceCommand::swap(
    Scene& scene,
    CommandHost& host,
    SubtreeSnapshot& from,
    SubtreeSnapshot& to
) {
    if (to.nodes.empty()) return;
    // Both snapshots share the root's slot, so tearing down what is there and
    // applying the target swaps in either direction. The root is checked against the side it is
    // on: a step may change what describes it (an unpacked instance loses its prefab uid).
    const EntityId root = from.liveRoot(scene);
    if (!root) {
        refuseOtherEntity(host, label(), from.nodes.front().snap.slotIndex);
        return;
    }
    // Into both: a slot only one side holds comes back when the swap next goes that way.
    keepDestroyedIds(scene, from);
    keepDestroyedIds(scene, to);
    HierarchyOperations::destroyHierarchy(scene, root);
    to.apply(scene, host.commandStack());
}

void DestroySubtreeCommand::redo(Scene& scene, CommandHost& host) {
    destroyCaptured(scene, host, m_snap, label());
}

void CreateSubtreeCommand::redo(Scene& scene, CommandHost& host) {
    // apply() has logged each slot it found taken.
    const EntityId root = m_snap.apply(scene, host.commandStack());
    if (!root) {
        toastOtherEntity(host, label());
        return;
    }
    host.selectEntity(root);
}

void CreateSubtreeCommand::undo(Scene& scene, CommandHost& host) {
    destroyCaptured(scene, host, m_snap, label());
}

void DestroySubtreeCommand::undo(Scene& scene, CommandHost& host) {
    // apply() has logged each slot it found taken.
    if (!m_snap.apply(scene, host.commandStack())) {
        toastOtherEntity(host, label());
        return;
    }
    if (m_priorSelection.slot() != 0) {
        for (const auto& node : m_snap.nodes) {
            if (node.snap.slotIndex == m_priorSelection.slot()) {
                host.selectEntity(scene.entityAt(node.snap.slotIndex));
                break;
            }
        }
    }
}

void PlacePrefabCommand::redo(Scene& scene, CommandHost& host) {
    // The original slot: a later command addresses the placed entity by index.
    const EntityId root = reclaimSlot(scene, m_rootSlot);
    if (!root) {
        refuseOtherEntity(host, label(), m_rootSlot);
        return;
    }

    // Pose first: instantiateInto keeps a Transform the root already carries.
    scene.add(root, Transform{m_at});
    scene.add(root, PrefabInstance{m_instance});

    const bool built = Prefab::instantiateInto(
        scene,
        *m_resources,
        m_instance.source,
        root,
        m_instance.overrides,
        nullptr,
        &m_builtSlots
    );
    if (!built) {
        // The subtree, not the root: a partial build has parented what it made.
        HierarchyOperations::destroyHierarchy(scene, root);
        // The prefab is re-read on every rebuild, so it can be gone or unreadable.
        const std::string name = std::filesystem::path(m_instance.source).filename().string();
        host.pushToast(ToastKind::Error, "Could not rebuild the instance of '" + name + "'");
        return;
    }
    if (m_parentSlot && scene.isAliveAtIndex(m_parentSlot)) {
        HierarchyOperations::setParent(scene, root, scene.entityAt(m_parentSlot));
    }
    for (auto [uid, slot] : Prefab::builtSlotsOf(scene, root)) {
        const auto was = m_destroyed.find(uid);
        if (was != m_destroyed.end()) host.commandStack().noteReturn(scene, was->second, scene.entityAt(slot));
    }
    if (const auto was = m_destroyed.find(PrefabEntity::ROOT); was != m_destroyed.end()) {
        host.commandStack().noteReturn(scene, was->second, root);
    }

    host.selectEntity(root);
}

void PlacePrefabCommand::undo(Scene& scene, CommandHost& host) {
    const EntityId root = scene.entityAt(m_rootSlot);
    if (!scene.isAlive(root)) return;
    const PrefabInstance* instance = scene.tryGet<PrefabInstance>(root);
    if (!instance || instance->source != m_instance.source) {
        refuseOtherEntity(host, label(), m_rootSlot);
        return;
    }
    m_builtSlots = Prefab::builtSlotsOf(scene, root);
    m_destroyed  = {{PrefabEntity::ROOT, root}};
    for (const auto& [uid, slot] : m_builtSlots) m_destroyed[uid] = scene.entityAt(slot);
    HierarchyOperations::destroyHierarchy(scene, root);
}

namespace {
void applyReparent(
    Scene& scene,
    EntityId child,
    EntityId capturedParent,
    EntityId capturedNext,
    const Transform& local
) {
    if (capturedParent) {
        const EntityId parent = liveEntity(scene, capturedParent);
        if (!parent) return;
        const EntityId next = liveEntity(scene, capturedNext);
        HierarchyOperations::setParent(scene, child, parent, siblingUnder(scene, parent, next.slot()));
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }
    if (Transform* at = scene.tryGet<Transform>(child)) *at = local;
}
} // namespace

void ReparentCommand::redo(Scene& scene, CommandHost& host) {
    const EntityId child = liveEntity(scene, m_child);
    if (!child) return;
    applyReparent(scene, child, m_newParent, m_newNext, m_after);
}

void ReparentCommand::undo(Scene& scene, CommandHost& host) {
    const EntityId child = liveEntity(scene, m_child);
    if (!child) return;
    applyReparent(scene, child, m_oldParent, m_oldNext, m_before);
}

EntityId pushGroup(Scene& scene, CommandHost& host, const std::vector<EntityId>& roots) {
    if (roots.empty()) return {};

    // Their common parent when they share one, the root otherwise.
    const auto parentOf = [&](EntityId id) {
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        return node ? node->parent : EntityId{};
    };
    EntityId parent = parentOf(roots.front());
    for (const EntityId root : roots) {
        if (parentOf(root) != parent) parent = {};
    }

    // At the middle of the box their positions span, in the parent's space.
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const EntityId root : roots) {
        const glm::vec3 at(HierarchyOperations::computeWorldMatrix(scene, root)[3]);
        low  = glm::min(low, at);
        high = glm::max(high, at);
    }
    const glm::vec3 middle = (low + high) * 0.5f;
    const glm::mat4 parentWorld =
        parent ? HierarchyOperations::computeWorldMatrix(scene, parent) : glm::mat4(1.0f);
    Transform placed;
    placed.position = glm::vec3(glm::inverse(parentWorld) * glm::vec4(middle, 1.0f));

    const EntityId group = scene.createEntity();
    scene.add(group, placed);
    scene.add(group, makeName("Group"));
    if (parent) HierarchyOperations::setParent(scene, group, parent);
    auto step = std::make_unique<CompositeCommand>("Group");
    EntitySnapshot made = EntitySnapshot::capture(scene, group);
    step->add(std::make_unique<CreateEntityCommand>(std::move(made), "Group", parent.slot()));

    // Each moves under it keeping its world pose, as a reparent in the Hierarchy does.
    const glm::mat4 toGroup = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, group));
    for (const EntityId root : roots) {
        const Hierarchy* node = scene.tryGet<Hierarchy>(root);
        const EntityId was  = node ? node->parent : EntityId{};
        const EntityId next = node ? node->nextSibling : EntityId{};
        Transform* local = scene.tryGet<Transform>(root);
        const Transform pose = local ? *local : Transform{};
        const glm::mat4 world = HierarchyOperations::computeWorldMatrix(scene, root);
        HierarchyOperations::setParent(scene, root, group);
        if (local) *local = Transform::fromModelMatrix(toGroup * world);
        const Transform after = local ? *local : Transform{};
        const EntityId last{};
        step->add(std::make_unique<ReparentCommand>(root, was, next, group, last, pose, after, "Group"));
    }
    host.pushStep(std::move(step));
    return group;
}

} // namespace Vkm::Engine
