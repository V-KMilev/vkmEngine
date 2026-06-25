#define VKM_LOG_CATEGORY "ASSET_COOK"

#include "cook/asset_cooker.h"

#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/hash/fnv1a.h"
#include "io/asset_cook.h"
#include "io/asset_library.h"
#include "io/asset_serializer.h"
#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Engine::AssetCooker {

namespace {

// Bump when the cook OUTPUT changes (vertex layout, mip policy, importer flags)
// to force every asset to re-cook on next save without touching recipes - it is
// folded into the recipe hash, so all stored hashes go stale at once.
constexpr uint32_t COOKER_VERSION = 1;

uint64_t hashRecipe(const nlohmann::json& recipe) {
    const std::string dump = recipe.dump();
    const uint64_t recipeHash = fnv1a64(dump);
    const uint32_t version = COOKER_VERSION;
    return fnv1a64(&version, sizeof(version), recipeHash);
}

bool writeRecipeFile(const std::filesystem::path& path, const std::string& name,
                     const char* typeTag, const nlohmann::json& source) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (!out) {
        LOG_ERROR("Cooker: cannot write recipe '%s'", path.string().c_str());
        return false;
    }
    nlohmann::json doc;
    doc["name"]   = name;
    doc["type"]   = typeTag;
    doc["source"] = source;
    out << doc.dump(2);
    return static_cast<bool>(out);
}

void cookMesh(const MeshAsset& mesh) {
    if (mesh.name.empty() || mesh.loading || !mesh.hasSource() || mesh.vertices.empty()) return;

    AssetLibrary& lib = AssetLibrary::get();
    const nlohmann::json& recipe = mesh.sourceJson();
    const uint64_t hash = hashRecipe(recipe);
    const std::string uid = AssetLibrary::uidFor(AssetType::Mesh, mesh.name);
    const std::string recipeRel = "meshes/" + uid + ".json";
    const std::string cookedRel = "meshes/" + uid + ".vkmc";

    const std::filesystem::path cookedPath = ProjectPaths::cooked() / cookedRel;
    const AssetLibrary::Record* existing = lib.find(AssetType::Mesh, mesh.name);
    std::error_code ec;
    if (existing && existing->recipeHash == hash && std::filesystem::exists(cookedPath, ec)) return;

    if (!writeRecipeFile(ProjectPaths::library() / recipeRel, mesh.name, "mesh", recipe)) return;
    if (!AssetCook::writeMesh(cookedPath, mesh, hash)) return;

    lib.upsert({AssetType::Mesh, mesh.name, recipeRel, cookedRel, hash});
    LOG_INFO("Cooked mesh '%s' (%zu verts, %zu indices)",
             mesh.name.c_str(), mesh.vertices.size(), mesh.indices.size());
}

void cookTexture(const TextureAsset& tex) {
    if (tex.name.empty() || tex.loading || !tex.hasSource() || tex.pixelData.empty()) return;

    AssetLibrary& lib = AssetLibrary::get();
    const nlohmann::json& recipe = tex.sourceJson();
    const uint64_t hash = hashRecipe(recipe);
    const std::string uid = AssetLibrary::uidFor(AssetType::Texture, tex.name);
    const std::string recipeRel = "textures/" + uid + ".json";
    const std::string cookedRel = "textures/" + uid + ".vkmc";

    const std::filesystem::path cookedPath = ProjectPaths::cooked() / cookedRel;
    const AssetLibrary::Record* existing = lib.find(AssetType::Texture, tex.name);
    std::error_code ec;
    if (existing && existing->recipeHash == hash && std::filesystem::exists(cookedPath, ec)) return;

    if (!writeRecipeFile(ProjectPaths::library() / recipeRel, tex.name, "texture", recipe)) return;
    if (!AssetCook::writeTexture(cookedPath, tex, hash)) return;

    lib.upsert({AssetType::Texture, tex.name, recipeRel, cookedRel, hash});
    LOG_INFO("Cooked texture '%s' (%ux%u)", tex.name.c_str(), tex.params.width, tex.params.height);
}

void cookMaterial(const MaterialAsset& mat, const ResourceManager& resources) {
    if (mat.name.empty()) return;

    AssetLibrary& lib = AssetLibrary::get();
    // A material's canonical inline form is both its editable source of truth and
    // its runtime form; no binary cook is needed.
    const nlohmann::json inlineSource = AssetSerializer::materialToInline(mat, resources);
    const uint64_t hash = hashRecipe(inlineSource);
    const std::string uid = AssetLibrary::uidFor(AssetType::Material, mat.name);
    const std::string recipeRel = "materials/" + uid + ".json";

    const AssetLibrary::Record* existing = lib.find(AssetType::Material, mat.name);
    if (existing && existing->recipeHash == hash) return;

    if (!writeRecipeFile(ProjectPaths::library() / recipeRel, mat.name, "material", inlineSource)) return;

    lib.upsert({AssetType::Material, mat.name, recipeRel, std::string{}, hash});
    LOG_INFO("Cooked material '%s'", mat.name.c_str());
}

} // namespace

void cookAllAssets(ResourceManager& resources) {
    LOG_INFO("Cooking assets into the library...");

    // Textures first, then materials (which reference textures by name), then
    // meshes - matching the load order so a downstream consumer is consistent.
    resources.forEachOfType<TextureAsset>([&](TextureHandle, const TextureAsset& tex) {
        if (!tex.hidden) cookTexture(tex);
    });
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle, const MaterialAsset& mat) {
        if (!mat.hidden) cookMaterial(mat, resources);
    });
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& mesh) {
        if (!mesh.hidden) cookMesh(mesh);
    });

    if (!AssetLibrary::get().save()) {
        LOG_ERROR("Cooker: failed to save the asset library manifest");
    }
}

} // namespace Engine::AssetCooker
