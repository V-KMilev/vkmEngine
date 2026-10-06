#define VKM_LOG_CATEGORY "EDITOR"

#include "command/editor_commands.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/component_serializer.h"
#include "io/scene/prefab.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"
#include "command/command_host.h"
#include "command/command_stack.h"
#include "command/component_edit.h"
#include "command/prefab_overrides.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Re-resolve a captured entity id through its slot.
 *
 * Undoing a delete resurrects the entity at its slot with a fresh generation, so
 * a held pre-delete id would fail its isAlive guard forever. Safe because the
 * stack is LIFO: an older command runs only once everything after it is undone,
 * and by then its slot holds the entity it was made against.
 *
 * @param scene    Scene the slot is looked up in.
 * @param captured The id as the command recorded it.
 * @return The entity now living in that slot, or an invalid id when none does.
 */
EntityId liveEntity(const Scene& scene, EntityId captured) {
    if (!captured || !scene.isAliveAtIndex(captured.slot())) return {};
    return scene.entityAt(captured.slot());
}

// createEntityAt asserts on a live slot, and a step reclaiming one cannot rule
// that out: the world may have been rebuilt underneath the history.
EntityId reclaimSlot(Scene& scene, uint32_t slot) {
    return scene.isAliveAtIndex(slot) ? EntityId{} : scene.createEntityAt(slot);
}

void toastOtherEntity(CommandHost& host, const char* label) {
    host.pushToast(ToastKind::Error, std::string("'") + label + "' skipped: another entity holds its slot");
}

void refuseOtherEntity(CommandHost& host, const char* label, uint32_t slot) {
    LOG_ERROR("'%s' skipped: slot %u holds another entity", label, slot);
    toastOtherEntity(host, label);
}

// Destroys a captured subtree, refused when its root's slot holds another entity.
void destroyCaptured(Scene& scene, CommandHost& host, const SubtreeSnapshot& snap, const char* label) {
    if (snap.nodes.empty()) return;
    const EntityId root = snap.liveRoot(scene);
    if (!root) {
        refuseOtherEntity(host, label, snap.nodes.front().snap.slotIndex);
        return;
    }
    HierarchyOperations::destroyHierarchy(scene, root);
}

} // namespace

bool CompositeCommand::tryMerge(Command& incoming) {
    // The label is the literal one call site passes, so it is the group's identity.
    auto* p = dynamic_cast<CompositeCommand*>(&incoming);
    if (!p || std::strcmp(p->m_label, m_label) != 0) return false;
    for (auto& step : p->m_commands) {
        bool merged = false;
        for (auto& held : m_commands) {
            if (held->tryMerge(*step)) {
                merged = true;
                break;
            }
        }
        if (!merged) m_commands.push_back(std::move(step));
    }
    return true;
}

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
void AddComponentCommand<T>::redo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || scene.has<T>(e)) return;
    T copy = m_value;
    scene.add(e, std::move(copy));
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
void RemoveComponentCommand<T>::undo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || scene.has<T>(e)) return;
    T copy = m_snapshot;
    scene.add(e, std::move(copy));
}

template <typename T>
void ComponentEditCommand<T>::redo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    T* held = scene.tryGet<T>(e);
    if (!held) return;
    *held = m_after;
}

template <typename T>
void ComponentEditCommand<T>::undo(Scene& scene, CommandHost&) {
    const EntityId e = liveEntity(scene, m_entity);
    T* held = scene.tryGet<T>(e);
    if (!held) return;
    *held = m_before;
}

template <typename T>
bool ComponentEditCommand<T>::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<ComponentEditCommand<T>*>(&incoming);
    if (!p || p->m_entity != m_entity) return false;
    m_after = p->m_after;
    return true;
}

#define VKM_EDITOR_INSTANTIATE_COMMAND(Type, field) \
    template class AddComponentCommand<Type>;       \
    template class RemoveComponentCommand<Type>;    \
    template class ComponentEditCommand<Type>;
VKM_EDITOR_COMMAND_COMPONENTS(VKM_EDITOR_INSTANTIATE_COMMAND)
#undef VKM_EDITOR_INSTANTIATE_COMMAND

template class AddComponentCommand<Name>;
template class ComponentEditCommand<Name>;

// swapValue replaces the contents, keeps the identity and bumps the version a
// cache watches. A snapshot assigned by hand would pin the version, so undo would
// change the asset but not the picture.
void MaterialEditCommand::step(const MaterialAsset& value) {
    if (!m_resources->isAlive(m_handle)) return;
    MaterialAsset next = value;
    m_resources->swapValue(m_handle, next);
}

void MaterialEditCommand::redo(Scene&, CommandHost&) {
    step(m_after);
}

void MaterialEditCommand::undo(Scene&, CommandHost&) {
    step(m_before);
}

bool MaterialEditCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<MaterialEditCommand*>(&incoming);
    if (!p || p->m_handle != m_handle) return false;

    // Copy-construct then move, as asset assignment is deleted. These were never in
    // the manager, so the identity the constructor leaves defaulted is nobody's.
    m_after = MaterialAsset(p->m_after);
    return true;
}

template <typename HandleType>
void RenameAssetCommand<HandleType>::redo(Scene&, CommandHost&) {
    if (!m_resources->isAlive(m_handle)) return;
    m_resources->rename(m_handle, m_after);
}

template <typename HandleType>
void RenameAssetCommand<HandleType>::undo(Scene&, CommandHost&) {
    if (!m_resources->isAlive(m_handle)) return;
    m_resources->rename(m_handle, m_before);
}

template class RenameAssetCommand<MaterialHandle>;
template class RenameAssetCommand<MeshHandle>;
template class RenameAssetCommand<TextureHandle>;
template class RenameAssetCommand<AnimationClipHandle>;
template class RenameAssetCommand<AudioClipHandle>;

namespace {

std::string scriptJsonOf(const ScriptComponent& script) {
    return ComponentSerializer::save(script).dump();
}

// Non-throwing: an exception would escape an undo mid ImGui frame with a scene
// half-rebuilt, so a document that does not parse adds nothing.
void addScriptFromJson(Scene& scene, EntityId id, const std::string& json) {
    constexpr bool ALLOW_EXCEPTIONS = false;
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, ALLOW_EXCEPTIONS);
    if (doc.is_discarded()) return;
    ScriptComponent sc;
    ComponentSerializer::load(doc, sc);
    scene.add(id, std::move(sc));
}

} // namespace

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

EntitySnapshot EntitySnapshot::capture(const Scene& scene, EntityId id) {
    EntitySnapshot s;
    s.slotIndex = id.slot();
#define VKM_SNAPSHOT_CAPTURE(Type, field) \
    if (const Type* held = scene.tryGet<Type>(id)) s.field = *held;
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_CAPTURE)
#undef VKM_SNAPSHOT_CAPTURE
    if (const ScriptComponent* script = scene.tryGet<ScriptComponent>(id)) {
        s.scriptJson = scriptJsonOf(*script);
    }
    return s;
}

void EntitySnapshot::apply(Scene& scene, EntityId id) const {
#define VKM_SNAPSHOT_APPLY(Type, fieldName)   \
    if (fieldName && !scene.has<Type>(id)) {  \
        Type v = *fieldName;                  \
        scene.add(id, std::move(v));          \
    }
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_APPLY)
#undef VKM_SNAPSHOT_APPLY
    if (scriptJson && !scene.has<ScriptComponent>(id)) addScriptFromJson(scene, id, *scriptJson);
}

bool EntitySnapshot::describes(const Scene& scene, EntityId id) const {
    const Name* liveName = scene.tryGet<Name>(id);
    if ((liveName != nullptr) != name.has_value()) return false;
    if (liveName && std::strcmp(liveName->value, name->value) != 0) return false;

    const PrefabEntity* liveUid = scene.tryGet<PrefabEntity>(id);
    if ((liveUid != nullptr) != prefabEntity.has_value()) return false;
    return !liveUid || liveUid->uid == prefabEntity->uid;
}

void CreateEntityCommand::redo(Scene& scene, CommandHost& host) {
    const EntityId e = reclaimSlot(scene, m_snap.slotIndex);
    if (!e) {
        refuseOtherEntity(host, label(), m_snap.slotIndex);
        return;
    }
    m_snap.apply(scene, e);
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
    scene.destroyEntity(id);
}

SubtreeSnapshot SubtreeSnapshot::capture(const Scene& scene, EntityId root) {
    SubtreeSnapshot s;
    if (!scene.isAlive(root)) return s;
    if (const Hierarchy* node = scene.tryGet<Hierarchy>(root)) {
        s.rootParentSlot = node->parent.slot();
    }
    // The set a delete destroys: parents before children, siblings in order.
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, root)) {
        Node n;
        n.snap = EntitySnapshot::capture(scene, id);
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        if (id != root && node) n.parentSlot = node->parent.slot();
        s.nodes.push_back(std::move(n));
    }
    return s;
}

bool SubtreeSnapshot::holds(uint32_t slotIndex) const {
    for (const Node& node : nodes) {
        if (node.snap.slotIndex == slotIndex) return true;
    }
    return false;
}

EntityId SubtreeSnapshot::liveRoot(const Scene& scene) const {
    if (nodes.empty()) return {};
    const EntityId root = scene.entityAt(nodes.front().snap.slotIndex);
    if (!scene.isAlive(root) || !nodes.front().snap.describes(scene, root)) return {};
    return root;
}

EntityId SubtreeSnapshot::apply(Scene& scene, SnapshotSlots slots) const {
    // Pass 1: recreate every entity, keeping the ids rather than asking the scene
    // later - a refused slot holds an unrelated entity. Null: did not come back.
    std::vector<EntityId> live(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        EntityId e;
        if (slots == SnapshotSlots::Reuse) {
            e = reclaimSlot(scene, node.snap.slotIndex);
            if (!e) {
                LOG_ERROR(
                    "SubtreeSnapshot::apply: slot %u is occupied; that entity is not coming back",
                    node.snap.slotIndex
                );
                continue;
            }
        } else {
            e = scene.createEntity();
        }
        node.snap.apply(scene, e);
        live[i] = e;
    }

    // Pass 1b: re-stamp entity references. Every entity came back with a new
    // generation, so a Joint restored verbatim is silently dropped by the solver.
    auto restamp = [&](EntityId stored) {
        if (!stored) return EntityId{};
        // Snapshot first: in the fresh case the originals are still alive, so
        // testing the scene first would answer with the original.
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].snap.slotIndex != stored.slot()) continue;
            return live[i];
        }
        return scene.isAlive(stored) ? stored : EntityId{};
    };

    for (size_t i = 0; i < nodes.size(); ++i) {
        const EntityId e = live[i];
        if (!e) continue;

        if (Joint* joint = scene.tryGet<Joint>(e)) {
            joint->connected = restamp(joint->connected);
        }
        if (Ragdoll* held = scene.tryGet<Ragdoll>(e)) {
            Ragdoll& ragdoll = *held;
            ragdoll.root = restamp(ragdoll.root);
            for (RagdollBone& bone : ragdoll.bones) bone.body = restamp(bone.body);
        }
    }
    // In captured order: setParent appends, so children come back in order.
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        const EntityId child = live[i];
        if (!child) continue;

        const uint32_t parentSlot = (node.parentSlot != 0) ? node.parentSlot : rootParentSlot;
        if (parentSlot == 0) continue;  // top-level

        // A parent inside the subtree is what pass 1 brought back (null if its
        // slot was taken); only one outside it is looked up.
        EntityId parent{};
        bool ours = false;
        for (size_t j = 0; j < nodes.size(); ++j) {
            if (nodes[j].snap.slotIndex != parentSlot) continue;
            parent = live[j];
            ours = true;
            break;
        }
        if (!ours) parent = scene.entityAt(parentSlot);

        if (!scene.isAlive(parent)) continue;  // external parent died in the meantime
        HierarchyOperations::setParent(scene, child, parent);
    }

    return live.empty() ? EntityId{} : live.front();
}

void SubtreeReplaceCommand::swapTo(Scene& scene, CommandHost& host, const SubtreeSnapshot& to) {
    if (to.nodes.empty()) return;
    // Both snapshots share the root's slot, so tearing down what is there and
    // applying the target swaps in either direction.
    const EntityId root = to.liveRoot(scene);
    if (!root) {
        refuseOtherEntity(host, label(), to.nodes.front().snap.slotIndex);
        return;
    }
    HierarchyOperations::destroyHierarchy(scene, root);
    to.apply(scene);
}

void DestroySubtreeCommand::redo(Scene& scene, CommandHost& host) {
    destroyCaptured(scene, host, m_snap, label());
}

void CreateSubtreeCommand::redo(Scene& scene, CommandHost& host) {
    // apply() has logged each slot it found taken.
    const EntityId root = m_snap.apply(scene);
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
    if (!m_snap.apply(scene)) {
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
    HierarchyOperations::destroyHierarchy(scene, root);
}

void PrefabOverrideCommand::step(
    Scene& scene,
    CommandHost& host,
    const std::vector<PrefabOverride>& entries
) {
    const bool applied = PrefabOverrides::apply(
        scene,
        *m_resources,
        liveEntity(scene, m_root),
        m_targetUid,
        m_component,
        entries
    );
    if (applied) return;
    const std::string message = "The prefab no longer defines " + m_component
        + " - the value here is stale until the scene is loaded again";
    host.pushToast(ToastKind::Warning, message);
}

void PrefabOverrideCommand::redo(Scene& scene, CommandHost& host) {
    step(scene, host, m_after);
}

void PrefabOverrideCommand::undo(Scene& scene, CommandHost& host) {
    step(scene, host, m_before);
}

bool PrefabOverrideCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<PrefabOverrideCommand*>(&incoming);
    if (!p || p->m_root != m_root || p->m_targetUid != m_targetUid
        || p->m_component != m_component) {
        return false;
    }
    m_after = p->m_after;
    return true;
}

namespace {
void applyReparent(Scene& scene, EntityId child, EntityId capturedParent, const Transform& local) {
    if (capturedParent) {
        const EntityId parent = liveEntity(scene, capturedParent);
        if (!parent) return;
        HierarchyOperations::setParent(scene, child, parent);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }
    if (Transform* at = scene.tryGet<Transform>(child)) *at = local;
}
} // namespace

void ReparentCommand::redo(Scene& scene, CommandHost& host) {
    const EntityId child = liveEntity(scene, m_child);
    if (!child) return;
    applyReparent(scene, child, m_newParent, m_after);
}

void ReparentCommand::undo(Scene& scene, CommandHost& host) {
    const EntityId child = liveEntity(scene, m_child);
    if (!child) return;
    applyReparent(scene, child, m_oldParent, m_before);
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
