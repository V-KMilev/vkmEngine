#define VKM_LOG_CATEGORY "ASSET_COOK"

#include "cook/asset_cooker.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/hash/fnv1a.h"
#include "core/reflect.h"
#include "io/asset/asset_cook.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Engine::AssetCooker {

namespace {

// Bump when the cook OUTPUT changes (vertex layout, mip policy, importer flags)
// to force assets to re-cook from their recipes on next save without touching
// those recipes - it is folded into the recipe hash, so all stored hashes go
// stale at once.
constexpr uint32_t COOKER_VERSION = 1;

uint64_t hashRecipe(const nlohmann::json& recipe) {
    const std::string dump = recipe.dump();
    const uint64_t recipeHash = fnv1a64(dump);
    const uint32_t version = COOKER_VERSION;
    return fnv1a64(&version, sizeof(version), recipeHash);
}

// An asset that came back from the cooked cache carries the loader's stand-in
// source, which records the name it was read by and nothing about where it came
// from. It is not a recipe: cooking it would overwrite the library's account of
// the import - the version-controlled source of truth - with a self-reference.
bool isCookedPlaceholder(const nlohmann::json& source) {
    return source.value("kind", std::string{}) == "cooked";
}

// An asset that cannot be cooked and that the library does not already hold is
// one the save writes a name-only reference to and the next load cannot resolve.
// Say so now, while whoever pressed save can still act on it.
void warnUnlisted(AssetType type, const std::string& name) {
    if (AssetLibrary::get().find(type, name)) return;
    LOG_WARNING("Cooker: %s '%s' has nothing to cook and no library entry; a scene "
                "referencing it will not load", Reflect::enumName(type), name.c_str());
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

// The cook* helpers return false only on a real cook failure (recipe / cooked
// write); a skip - unnamed, nothing to bake, or already up to date - returns
// true, so cookAllAssets can distinguish failures from no-ops.
bool cookMesh(const MeshAsset& mesh) {
    if (mesh.name.empty()) return true;

    // Nothing to bake: still streaming in, never had a recipe, its decode failed,
    // or it was read back from the cooked cache and the library already holds the
    // recipe it was baked from.
    if (mesh.loading || !mesh.hasSource() || mesh.vertices.empty()
        || isCookedPlaceholder(mesh.sourceJson())) {
        warnUnlisted(AssetType::Mesh, mesh.name);
        return true;
    }

    AssetLibrary& lib = AssetLibrary::get();
    const nlohmann::json& recipe = mesh.sourceJson();
    const uint64_t hash = hashRecipe(recipe);

    const std::filesystem::path recipePath = AssetLibrary::recipePath(AssetType::Mesh, mesh.name);
    const std::filesystem::path cookedPath = AssetLibrary::cookedPath(AssetType::Mesh, mesh.name);
    const AssetRecord* existing = lib.find(AssetType::Mesh, mesh.name);
    std::error_code ec;
    if (existing && existing->recipeHash == hash && std::filesystem::exists(cookedPath, ec)) return true;

    if (!writeRecipeFile(recipePath, mesh.name, "mesh", recipe)) return false;
    if (!AssetCook::writeMesh(cookedPath, mesh, hash)) return false;

    lib.upsert({AssetType::Mesh, mesh.name, hash});
    LOG_INFO("Cooked mesh '%s' (%zu verts, %zu indices)",
             mesh.name.c_str(), mesh.vertices.size(), mesh.indices.size());
    return true;
}

bool cookTexture(const TextureAsset& tex) {
    if (tex.name.empty()) return true;

    if (tex.loading || !tex.hasSource() || tex.pixelData.empty()
        || isCookedPlaceholder(tex.sourceJson())) {
        warnUnlisted(AssetType::Texture, tex.name);
        return true;
    }

    AssetLibrary& lib = AssetLibrary::get();
    const nlohmann::json& recipe = tex.sourceJson();
    const uint64_t hash = hashRecipe(recipe);

    const std::filesystem::path recipePath = AssetLibrary::recipePath(AssetType::Texture, tex.name);
    const std::filesystem::path cookedPath = AssetLibrary::cookedPath(AssetType::Texture, tex.name);
    const AssetRecord* existing = lib.find(AssetType::Texture, tex.name);
    std::error_code ec;
    if (existing && existing->recipeHash == hash && std::filesystem::exists(cookedPath, ec)) return true;

    if (!writeRecipeFile(recipePath, tex.name, "texture", recipe)) return false;
    if (!AssetCook::writeTexture(cookedPath, tex, hash)) return false;

    lib.upsert({AssetType::Texture, tex.name, hash});
    LOG_INFO("Cooked texture '%s' (%ux%u)", tex.name.c_str(), tex.params.width, tex.params.height);
    return true;
}

bool cookMaterial(const MaterialAsset& mat, const ResourceManager& resources) {
    if (mat.name.empty()) return true;

    AssetLibrary& lib = AssetLibrary::get();
    // A material's canonical inline form is both its editable source of truth and
    // its runtime form; no binary cook is needed.
    const nlohmann::json inlineSource = AssetSerializer::materialToInline(mat, resources);
    const uint64_t hash = hashRecipe(inlineSource);

    const std::filesystem::path recipePath = AssetLibrary::recipePath(AssetType::Material, mat.name);
    const AssetRecord* existing = lib.find(AssetType::Material, mat.name);
    if (existing && existing->recipeHash == hash) return true;

    if (!writeRecipeFile(recipePath, mat.name, "material", inlineSource)) return false;

    lib.upsert({AssetType::Material, mat.name, hash});
    LOG_INFO("Cooked material '%s'", mat.name.c_str());
    return true;
}

} // namespace

void cookAllAssets(ResourceManager& resources) {
    LOG_INFO("Cooking assets into the library...");

    // Textures first, then materials (which reference textures by name), then
    // meshes - matching the load order so a downstream consumer is consistent.
    size_t failed = 0;
    resources.forEachOfType<TextureAsset>([&](TextureHandle, const TextureAsset& tex) {
        if (!tex.hidden && !cookTexture(tex)) ++failed;
    });
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle, const MaterialAsset& mat) {
        if (!mat.hidden && !cookMaterial(mat, resources)) ++failed;
    });
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& mesh) {
        if (!mesh.hidden && !cookMesh(mesh)) ++failed;
    });

    // A failed cook leaves the manifest referencing a cooked file that was never
    // written; surface it instead of saving silently as if everything succeeded.
    if (failed > 0) {
        LOG_ERROR("Cooker: %zu asset(s) failed to cook; manifest may reference missing cooked files", failed);
    }

    if (!AssetLibrary::get().save()) {
        LOG_ERROR("Cooker: failed to save the asset library manifest");
    }
}

} // namespace Engine::AssetCooker
