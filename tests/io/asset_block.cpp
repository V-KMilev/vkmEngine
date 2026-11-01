// A scene's assets block: what its save names so its next load can build it, every asset its
// entities hold and what those assets name in turn, each once.

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/effect/decal.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/scene.h"
#include "io/scene/scene_assets.h"
#include "resource/resource_manager.h"

#include "harness/check.h"

using namespace Vkm::Engine;

namespace {

/// The names a section of @p block lists, in its order.
std::vector<std::string> section(const nlohmann::json& block, const char* key) {
    std::vector<std::string> names;
    for (const nlohmann::json& entry : block.at(key)) names.push_back(entry.at("name").get<std::string>());
    return names;
}

using Names = std::vector<std::string>;

} // namespace

TEST("a scene's assets block names every asset its entities hold and what those name, once each, in order") {
    ResourceManager resources;
    Scene scene;
    const auto texture = [&](const char* name) { return resources.add(TextureAsset{}, name); };

    MaterialAsset boxLook;
    boxLook.albedoTexture = texture("box:albedo");
    boxLook.normalTexture = texture("box:normal");
    const MaterialHandle box = resources.add(std::move(boxLook), "box");
    MaterialAsset stain;
    stain.albedoTexture = texture("stain:albedo");
    const MaterialHandle decal = resources.add(std::move(stain), "stain");
    MaterialAsset groundLook;
    groundLook.albedoTexture = texture("ground:albedo");
    const MaterialHandle ground = resources.add(std::move(groundLook), "ground");
    // A thumbnail's material is the editor's, never the scene's.
    MaterialAsset preview;
    preview.albedoTexture = texture("preview:albedo");
    const MaterialHandle hidden = resources.addPrivate(std::move(preview), "preview");

    TerrainAsset field;
    field.layers[0] = ground;
    TerrainFoliage   grass;
    TerrainPlantPart blade;
    blade.mesh     = resources.add(MeshAsset{}, "blade");
    blade.material = ground;
    grass.parts.push_back(blade);
    field.foliage.push_back(grass);
    const TerrainHandle terrain = resources.add(std::move(field), "field");
    const MeshHandle crate = resources.add(MeshAsset{}, "crate");

    std::vector<EntityId> entities;
    const auto entity = [&]() { return entities.emplace_back(scene.createEntity()); };
    const auto drawn = [](MeshHandle mesh, MaterialHandle material) {
        Mesh out;
        out.mesh     = mesh;
        out.material = material;
        return out;
    };
    scene.add(entity(), drawn(crate, box));
    scene.add(entity(), drawn(crate, box));  // held twice, named once
    scene.add(entity(), drawn(crate, hidden));
    UIImage image;
    image.texture = texture("ui:badge");
    scene.add(entity(), image);
    ParticleEmitter puff;
    puff.texture   = texture("puff");
    puff.normalMap = texture("puff:normal");
    scene.add(entity(), puff);
    Decal mark;
    mark.material = decal;
    scene.add(entity(), mark);
    AudioSource chime;
    chime.clip = resources.add(AudioClipAsset{}, "chime");
    scene.add(entity(), chime);
    Animator animator;
    animator.skeleton = resources.add(SkeletonAsset{}, "rig");
    animator.clip     = resources.add(AnimationClipAsset{}, "walk");
    scene.add(entity(), animator);
    Terrain land;
    land.terrain = terrain;
    scene.add(entity(), land);
    resources.add(MeshAsset{}, "unused");

    const nlohmann::json block = SceneAssets::saveAssetsForEntities(scene, entities, resources);
    // Held textures first, then each material's maps as the material is met, then the terrain's.
    const Names textures = {
        "ui:badge", "puff", "puff:normal", "box:albedo", "box:normal", "stain:albedo", "ground:albedo",
    };
    CHECK(section(block, "textures") == textures);
    CHECK(section(block, "materials") == Names({"box", "stain", "ground"}));
    CHECK(section(block, "meshes") == Names({"crate", "blade"}));
    CHECK(section(block, "sounds") == Names({"chime"}));
    CHECK(section(block, "skeletons") == Names({"rig"}));
    CHECK(section(block, "clips") == Names({"walk"}));
    CHECK(section(block, "terrains") == Names({"field"}));

    // The whole graph's block lists the unheld too, and still nothing hidden.
    const nlohmann::json all = SceneAssets::saveAllAssets(resources);
    const Names meshes = section(all, "meshes");
    CHECK(std::find(meshes.begin(), meshes.end(), "unused") != meshes.end());
    const Names materials = section(all, "materials");
    CHECK(std::find(materials.begin(), materials.end(), "preview") == materials.end());
}
