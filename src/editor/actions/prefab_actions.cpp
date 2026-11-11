#include "actions/prefab_actions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "io/project/roots.h"
#include "undo/entity_commands.h"
#include "undo/entity_snapshot.h"
#include "editor_state.h"
#include "view/view_placement.h"
#include "undo/prefab_overrides.h"
#include "ecs/scene.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"
#include "resource/resource_manager.h"
#include "session/project/content_pack.h"
#include "session/asset/asset_writes.h"
#include "ui/entity_labels.h"
namespace Vkm::Engine {
namespace EditorActions {

namespace {

// A name as a filename: only safe characters, and a fallback over an unopenable one.
std::string prefabStem(const Scene& scene, EntityId entity) {
    const Name* named = scene.tryGet<Name>(entity);
    std::string stem  = named ? named->value : "";
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return stem.empty() ? "prefab" : stem;
}

// First prefab file name not already on disk: `base`, then "base 2", ...
std::string uniquePrefabStem(const std::string& base) {
    std::error_code ec;
    std::string stem = base;
    for (int n = 2; std::filesystem::exists(Roots::prefabs() / (stem + PREFAB_EXTENSION), ec); ++n) {
        stem = base + " " + std::to_string(n);
    }
    return stem;
}

} // namespace

std::vector<std::string> listPrefabs() {
    std::vector<std::string> prefabs;
    std::error_code          ec;
    // Stepped with the error code: a range-for's covers the constructor alone.
    std::filesystem::recursive_directory_iterator walk(Roots::prefabs(), ec), end;
    for (; !ec && walk != end; walk.increment(ec)) {
        // Its own code, so one entry that cannot be read is skipped rather than ending the walk.
        std::error_code entry;
        if (!walk->is_regular_file(entry) || walk->path().extension() != PREFAB_EXTENSION) continue;
        prefabs.push_back(Roots::toProjectRelative(walk->path().string()));
    }
    std::sort(prefabs.begin(), prefabs.end());
    return prefabs;
}

void unpackPrefab(Scene& scene, EditorState& state, EntityId root) {
    if (!scene.has<PrefabInstance>(root)) return;
    SubtreeSnapshot before = SubtreeSnapshot::capture(scene, root);
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, root)) scene.remove<PrefabEntity>(id);
    scene.remove<PrefabInstance>(root);

    SubtreeSnapshot after = SubtreeSnapshot::capture(scene, root);
    auto step = std::make_unique<SubtreeReplaceCommand>(std::move(before), std::move(after), "Unpack Prefab");
    state.pushStep(std::move(step));
}

bool saveAsPrefab(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity) {
    if (!scene.isAlive(entity)) return false;

    // Prefab::save refuses this too; here the instance root can be named.
    const EntityId owner = Prefab::instanceRootOf(scene, entity);
    if (owner && owner != entity) {
        char rootName[64];
        getEntityDisplayName(scene, owner, rootName, sizeof(rootName));
        const std::string message = std::string("This entity belongs to a prefab - Save as Prefab on '")
            + rootName + "', the instance root, writes it with the rest";
        state.toasts.push(ToastKind::Warning, message);
        return false;
    }

    // An instance saves over its own source, anything else takes a free name.
    // Project-relative: the instance stores it, and scenes move between machines.
    const PrefabInstance* instance = scene.tryGet<PrefabInstance>(entity);
    const bool editsItsOwn = instance && !instance->source.empty();
    std::string path = editsItsOwn ? instance->source : std::string{};
    if (!editsItsOwn) {
        const std::string file = uniquePrefabStem(prefabStem(scene, entity)) + PREFAB_EXTENSION;
        path = Roots::toProjectRelative((Roots::prefabs() / file).string());
    }

    // As a scene's save does, so a later session resolves what the prefab names.
    const AssetWrites writes = writeAssets(resources);

    const std::string shown = std::filesystem::path(path).stem().string();
    if (!Prefab::save(scene, entity, path, resources)) {
        state.toasts.push(ToastKind::Error, "Could not save prefab '" + shown + "'");
        return false;
    }

    // The walk Prefab::save wrote with, so no undo step addresses a component the
    // scene stopped storing; see docs/reference/editor.md, "Save as Prefab".
    std::vector<uint32_t> subtreeSlots;
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, entity)) {
        subtreeSlots.push_back(id.slot());
    }
    state.commands.forget(subtreeSlots);
    state.markSceneDirty();
    if (!writes.terrains) {
        const std::string message = "Saved prefab '" + shown + "', but a terrain could not be written";
        state.toasts.push(ToastKind::Error, message);
    }
    if (!writes.cooked) {
        state.toasts.push(ToastKind::Error, "Saved prefab '" + shown + "', but some assets did not cook");
    }
    if (writes.terrains && writes.cooked) state.toasts.push(ToastKind::Info, "Saved prefab '" + shown + "'");
    return true;
}

EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& path) {
    Transform at;
    at.position = ViewPlacement::placeInView(scene, resources, state.viewAim, 0.0f);
    const EntityId root = Prefab::instantiate(scene, resources, path, at);
    if (!root) {
        const std::string name = std::filesystem::path(path).filename().string();
        state.toasts.push(ToastKind::Error, "Could not place prefab '" + name + "'");
        return {};
    }

    state.pushStep(
        std::make_unique<PlacePrefabCommand>(
            resources,
            scene.get<PrefabInstance>(root),
            root,
            at,
            "Place Prefab"
        )
    );
    state.selectEntity(root);
    return root;
}

std::vector<std::string> shippedEffects() {
    return ContentPack::prefabsIn(ContentPack::shipped("effects"), "effects");
}

EntityId placeEffect(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& file) {
    const std::filesystem::path pack = ContentPack::shipped("effects");
    std::string error;
    const int copied = ContentPack::bringInto(pack, Roots::projectRoot(), error);
    if (copied < 0) {
        state.toasts.push(ToastKind::Error, "Could not bring the engine's effects in: " + error);
        return {};
    }
    if (copied > 0) {
        state.toasts.push(
            ToastKind::Info,
            "Brought the engine's effects into the project (" + std::to_string(copied) + " files)"
        );
    }
    return placePrefab(scene, resources, state, "prefabs/effects/" + file);
}

} // namespace EditorActions
} // namespace Vkm::Engine
