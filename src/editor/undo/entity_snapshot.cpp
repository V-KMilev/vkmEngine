#define VKM_LOG_CATEGORY "EDITOR"

#include "undo/entity_snapshot.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/component_serializer.h"
#include "system/script/script_component.h"
#include "undo/command_stack.h"
#include "vkm_log/assert.h"
#include "vkm_log/logger.h"

namespace Vkm::Engine {

EntityId liveEntity(const Scene& scene, EntityId captured) {
    if (!captured || !scene.isAliveAtIndex(captured.slot())) return {};
    return scene.entityAt(captured.slot());
}

EntityId siblingUnder(const Scene& scene, EntityId parent, uint32_t slot) {
    if (slot == 0 || !scene.isAliveAtIndex(slot)) return {};
    const EntityId sibling = scene.entityAt(slot);
    const Hierarchy* node  = scene.tryGet<Hierarchy>(sibling);
    return node && node->parent == parent ? sibling : EntityId{};
}

EntityId reclaimSlot(Scene& scene, uint32_t slot) {
    return scene.isAliveAtIndex(slot) ? EntityId{} : scene.createEntityAt(slot);
}

std::string scriptJsonOf(const ScriptComponent& script) {
    return ComponentSerializer::save(script).dump();
}

void addScriptFromJson(Scene& scene, EntityId id, const std::string& json) {
    constexpr bool ALLOW_EXCEPTIONS = false;
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, ALLOW_EXCEPTIONS);
    if (doc.is_discarded()) return;
    ScriptComponent sc;
    ComponentSerializer::load(doc, sc);
    scene.add(id, std::move(sc));
}

namespace {

// Every entity @p component names, mapped through @p restamp: the component goes through its own
// scene serializer, which knows its entity fields, and comes back re-pointed. Only when a reference
// moves, as a load resets what the file does not hold: a live ragdoll's pose.
template <typename T, typename Restamp>
void restampComponent(T& component, const Restamp& restamp) {
    std::vector<EntityId> named;
    auto keep = [&](EntityId ref) {
        named.push_back(ref);
        return static_cast<uint32_t>(named.size());
    };
    const nlohmann::json saved = ComponentSerializer::save(component, EntityNamer(keep));
    const auto moves = [&](EntityId ref) { return restamp(ref) != ref; };
    if (std::none_of(named.begin(), named.end(), moves)) return;
    auto back = [&](uint32_t index) { return index != 0 ? restamp(named[index - 1]) : EntityId{}; };
    ComponentSerializer::load(saved, component, EntityResolver(back));
}

// restampComponent over every component of @p e that names entities.
template <typename Restamp>
void restampEntity(Scene& scene, EntityId e, const Restamp& restamp) {
#define VKM_RESTAMP_NONE(Type, Key)
#define VKM_RESTAMP(Type, Key) if (Type* held = scene.tryGet<Type>(e)) restampComponent(*held, restamp);
    VKM_SCENE_COMPONENTS(VKM_RESTAMP_NONE, VKM_RESTAMP_NONE, VKM_RESTAMP)
#undef VKM_RESTAMP
#undef VKM_RESTAMP_NONE
}

} // namespace

void followReturns(Scene& scene, EntityId id, const CommandStack& history) {
    restampEntity(scene, id, [&](EntityId named) { return history.returnOf(scene, named); });
}

EntitySnapshot EntitySnapshot::capture(const Scene& scene, EntityId id) {
    EntitySnapshot s;
    s.slotIndex = id.slot();
    const auto take = [&](auto& slot) {
        using T = typename std::decay_t<decltype(slot)>::value_type;
        if (const T* held = scene.tryGet<T>(id)) slot = *held;
    };
    std::apply([&](auto&... slot) { (take(slot), ...); }, s.values);
    if (const ScriptComponent* script = scene.tryGet<ScriptComponent>(id)) s.script = scriptJsonOf(*script);
    return s;
}

void EntitySnapshot::apply(Scene& scene, EntityId id) const {
    const auto give = [&](const auto& slot) {
        using T = typename std::decay_t<decltype(slot)>::value_type;
        if (slot && !scene.has<T>(id)) scene.add(id, T(*slot));
    };
    std::apply([&](const auto&... slot) { (give(slot), ...); }, values);
    if (script && !scene.has<ScriptComponent>(id)) addScriptFromJson(scene, id, *script);
}

bool EntitySnapshot::describes(const Scene& scene, EntityId id) const {
    const std::optional<Name>& name = get<Name>();
    const Name* liveName = scene.tryGet<Name>(id);
    if ((liveName != nullptr) != name.has_value()) return false;
    if (liveName && std::strcmp(liveName->value, name->value) != 0) return false;

    const std::optional<PrefabEntity>& prefabEntity = get<PrefabEntity>();
    const PrefabEntity* liveUid = scene.tryGet<PrefabEntity>(id);
    if ((liveUid != nullptr) != prefabEntity.has_value()) return false;
    return !liveUid || liveUid->uid == prefabEntity->uid;
}

SubtreeSnapshot SubtreeSnapshot::capture(const Scene& scene, EntityId root) {
    SubtreeSnapshot s;
    if (!scene.isAlive(root)) return s;
    if (const Hierarchy* node = scene.tryGet<Hierarchy>(root)) {
        s.rootParentSlot = node->parent.slot();
        s.rootNextSlot   = node->nextSibling.slot();
    }
    // The set a delete destroys: parents before children, siblings in order.
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, root)) {
        Node n;
        n.snap   = EntitySnapshot::capture(scene, id);
        n.entity = id;
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

namespace {

// SubtreeSnapshot::apply()'s work: every node of @p snap's entity, in node order; null where one did
// not come back. @p history is the one apply() was given when @p slots is SnapshotSlots::Reuse.
std::vector<EntityId> applyNodes(
    const SubtreeSnapshot& snap,
    Scene& scene,
    SnapshotSlots slots,
    CommandStack* history
) {
    const auto& nodes = snap.nodes;
    const uint32_t rootParentSlot = snap.rootParentSlot;
    const uint32_t rootNextSlot   = snap.rootNextSlot;
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
        if (history) history->noteReturn(scene, node.entity, e);
    }

    // Pass 1b: re-stamp entity references. Every entity came back with a new
    // generation, so a reference restored verbatim - a Joint's, a slider's knob - points at none.
    auto restamp = [&](EntityId stored) {
        if (!stored) return EntityId{};
        // Snapshot first: in the fresh case the originals are still alive, so
        // testing the scene first would answer with the original.
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].snap.slotIndex != stored.slot()) continue;
            return live[i];
        }
        // Back in its own slots, a reference outside follows its entity's returns, or keeps the id for
        // pass 1c of the return still to come.
        if (history) return history->returnOf(scene, stored);
        return slots != SnapshotSlots::Foreign && scene.isAlive(stored) ? stored : EntityId{};
    };

    for (const EntityId e : live) {
        if (e) restampEntity(scene, e, restamp);
    }

    // Pass 1c, back in their own slots: a reference from outside the subtree to the id an entity was
    // destroyed under follows it back, as no other entity ever had that id.
    if (slots == SnapshotSlots::Reuse) {
        const auto follow = [&](EntityId stored) {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (live[i] && nodes[i].entity == stored) return live[i];
            }
            return stored;
        };
        const auto followIn = [&](auto& held) { restampComponent(held, follow); };
#define VKM_FOLLOW_NONE(Type, Key)
#define VKM_FOLLOW(Type, Key) scene.forEach<Type>([&](EntityId, Type& held) { followIn(held); });
        VKM_SCENE_COMPONENTS(VKM_FOLLOW_NONE, VKM_FOLLOW_NONE, VKM_FOLLOW)
#undef VKM_FOLLOW
#undef VKM_FOLLOW_NONE
    }

    // In captured order: setParent appends, so children come back in order.
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node     = nodes[i];
        const EntityId child = live[i];
        if (!child) continue;

        const uint32_t rootParent = slots == SnapshotSlots::Foreign ? 0 : rootParentSlot;
        const uint32_t parentSlot = (node.parentSlot != 0) ? node.parentSlot : rootParent;
        if (parentSlot == 0) continue;  // top-level

        // A parent inside the subtree is what pass 1 brought back (null if its
        // slot was taken); only one outside it is looked up.
        EntityId parent{};
        bool ours = false;
        for (size_t j = 0; j < nodes.size(); ++j) {
            if (nodes[j].snap.slotIndex != parentSlot) continue;
            parent = live[j];
            ours   = true;
            break;
        }
        if (!ours) parent = scene.entityAt(parentSlot);

        if (!scene.isAlive(parent)) continue;  // external parent died in the meantime
        // The root back where it stood among its siblings.
        const EntityId before = i == 0 ? siblingUnder(scene, parent, rootNextSlot) : EntityId{};
        HierarchyOperations::setParent(scene, child, parent, before);
    }

    return live;
}

} // namespace

EntityId SubtreeSnapshot::apply(Scene& scene, CommandStack& history) const {
    const std::vector<EntityId> live = applyNodes(*this, scene, SnapshotSlots::Reuse, &history);
    return live.empty() ? EntityId{} : live.front();
}

EntityId SubtreeSnapshot::apply(Scene& scene, SnapshotSlots slots) const {
    VKM_ASSERT(slots != SnapshotSlots::Reuse, "SubtreeSnapshot::apply: a restore goes through the history");
    const std::vector<EntityId> live = applyNodes(*this, scene, slots, nullptr);
    return live.empty() ? EntityId{} : live.front();
}

std::vector<EntityId> SubtreeSnapshot::applyTogether(
    Scene& scene,
    const std::vector<SubtreeSnapshot>& trees
) {
    // One snapshot of them all, so a reference from one to another is re-stamped, not dropped.
    SubtreeSnapshot all;
    std::vector<size_t> roots;
    roots.reserve(trees.size());
    for (const SubtreeSnapshot& tree : trees) {
        roots.push_back(all.nodes.size());
        all.nodes.insert(all.nodes.end(), tree.nodes.begin(), tree.nodes.end());
    }
    const std::vector<EntityId> live = applyNodes(all, scene, SnapshotSlots::Foreign, nullptr);
    std::vector<EntityId> made;
    made.reserve(roots.size());
    for (size_t i = 0; i < roots.size(); ++i) {
        const bool empty = trees[i].nodes.empty();
        made.push_back(empty ? EntityId{} : live[roots[i]]);
    }
    return made;
}

} // namespace Vkm::Engine
