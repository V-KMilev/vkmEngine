#include "actions/asset_actions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

#include "io/asset/asset_library.h"
#include "io/project/roots.h"
#include "undo/component_edit.h"
#include "editor_state.h"
#include "ecs/scene.h"
#include "ecs/component/render/mesh.h"
#include "io/asset/recipe/asset_source.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/terrain_asset.h"
#include "io/asset/generate/material_generators.h"
namespace Vkm::Engine {
namespace EditorActions {

namespace {

// First material name not already taken: `base`, then "base 2", "base 3", ...
// Neither loaded nor in the library: a save would write over a library material not loaded.
std::string uniqueMaterialName(ResourceManager& resources, const std::string& base) {
    const auto taken = [&](const std::string& name) {
        return static_cast<bool>(resources.findByName<MaterialAsset>(name))
            || AssetLibrary::get().find(AssetType::Material, name) != nullptr;
    };
    std::string name = base;
    for (int n = 2; taken(name); ++n) name = base + " " + std::to_string(n);
    return name;
}

// The name a copy of @p base is given before it is made unique: a new material's for one the engine
// makes ("material:default"), which no project names; else the base's, less an earlier copy's suffix,
// so a copy of "Brick copy" is "Brick copy 2", not "Brick copy copy".
std::string copyNameOf(const std::string& base) {
    if (base.rfind("material:", 0) == 0) return "Material";
    const size_t at     = base.rfind(" copy");
    const auto tail     = at == std::string::npos ? std::string() : base.substr(at + 5);
    const bool isDigits = tail.size() > 1 && tail[0] == ' '
        && std::all_of(tail.begin() + 1, tail.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    const bool numbered = at != std::string::npos && (tail.empty() || isDigits);
    return (numbered ? base.substr(0, at) : base) + " copy";
}

} // namespace

MaterialHandle copyMaterial(ResourceManager& resources, MaterialHandle base, const std::string& name) {
    MaterialAsset copy = resources.get(base);
    return resources.add(std::move(copy), uniqueMaterialName(resources, name));
}

MaterialHandle forkMaterial(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity,
    MaterialHandle material
) {
    if (!material) return MaterialHandle{};
    const MaterialHandle fork = copyMaterial(resources, material, copyNameOf(resources.get(material).name()));
    if (!fork) return fork;

    const Mesh* mesh = scene.isAlive(entity) ? scene.tryGet<Mesh>(entity) : nullptr;
    if (mesh && mesh->material == material) {
        EditScope<Mesh> edit(scene, resources, state, entity, "Duplicate Material");
        edit->material = fork;
    }
    state.openMaterial(fork);
    return fork;
}

MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state) {
    const MaterialHandle base = generateDefaultMaterial(resources);
    if (!base) return MaterialHandle{};

    const MaterialHandle h = copyMaterial(resources, base, "Material");
    if (!h) return MaterialHandle{};

    state.markSceneDirty();
    return h;
}

TerrainHandle createNewTerrain(ResourceManager& resources) {
    // A path neither a loaded terrain nor a file on disk holds.
    std::string name;
    for (int n = 1;; ++n) {
        name = "assets/terrain/terrain" + (n == 1 ? std::string{} : "_" + std::to_string(n)) + ".terrain";
        std::error_code ec;
        const bool onDisk = std::filesystem::exists(Roots::resolveProjectPath(name), ec);
        if (!resources.findByName<TerrainAsset>(name) && !onDisk) break;
    }
    TerrainAsset terrain;
    const size_t vertices = static_cast<size_t>(terrain.resolution) * terrain.resolution;
    terrain.heights.assign(vertices, 0.0f);
    terrain.weights.assign(vertices * TerrainAsset::WEIGHTS_PER_VERTEX, 0);
    for (size_t v = 0; v < vertices; ++v) terrain.weights[v * TerrainAsset::WEIGHTS_PER_VERTEX] = 255;
    terrain.layers[0] = generateDefaultMaterial(resources);
    terrain.sourceJson() = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, name}};
    return resources.add(std::move(terrain), name);
}

} // namespace EditorActions
} // namespace Vkm::Engine
