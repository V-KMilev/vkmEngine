#define VKM_LOG_CATEGORY "LOADER"

#include "loader/material_loaders.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <optional>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "loader/texture_loaders.h"
#include "generator/texture_generators.h"

namespace Vkm::Engine {

namespace {

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

/**
 * @brief Does @p filename contain @p pattern as a whole word?
 *
 * Acronyms are matched on token boundaries, never as bare substrings. "orm"
 * and "rma" are both inside the word *normal* (n-orm-al, no-rma-l), and "ao"
 * is inside plenty of ordinary words, so substring matching handed the normal
 * map to the packed metallic-roughness slot for practically every PBR folder
 * that has one. Roughness then came out of the normal map's green channel and
 * metallic out of its blue - which is near 1.0, so the surface read as solid
 * metal - and the real maps were skipped, because a packed map suppresses them.
 *
 * Only short patterns are boundary-checked. The longer ones are words rather
 * than acronyms, they collide with nothing in practice, and tightening them
 * would stop matching spellings that work today ("normalmap" no longer
 * containing a bounded "normal", say).
 */
bool nameMatchesPattern(const std::string& filename, const std::string& pattern) {
    constexpr size_t ACRONYM_MAX = 3;
    if (pattern.size() > ACRONYM_MAX) return filename.find(pattern) != std::string::npos;

    const auto isWordChar = [](unsigned char c) { return std::isalnum(c) != 0; };
    for (size_t at = filename.find(pattern); at != std::string::npos;
         at = filename.find(pattern, at + 1)) {
        const bool leftOk  = at == 0 || !isWordChar(static_cast<unsigned char>(filename[at - 1]));
        const size_t after = at + pattern.size();
        const bool rightOk = after >= filename.size()
                          || !isWordChar(static_cast<unsigned char>(filename[after]));
        if (leftOk && rightOk) return true;
    }
    return false;
}

// First file matching one of the patterns (case-insensitive) and carrying one
// of the extensions: "color" finds "PavingStones_Color.jpg" or "brick_color.png".
std::optional<std::string> findTexture(
    const std::string& folderPath,
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& extensions = {".jpg", ".jpeg", ".png", ".tga", ".bmp"}
) {
    const std::filesystem::path folder = ProjectPaths::resolveProjectPath(folderPath);
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::string extension = entry.path().extension().string();

        std::string extensionLower = toLower(extension);
        bool hasValidExtension = false;
        for (const auto& ext : extensions) {
            if (extensionLower == toLower(ext)) {
                hasValidExtension = true;
                break;
            }
        }
        if (!hasValidExtension) continue;

        std::string filenameLower = toLower(filename);
        for (const auto& pattern : patterns) {
            std::string patternLower = toLower(pattern);

            if (nameMatchesPattern(filenameLower, patternLower)) {
                // The reference, not the walked absolute: it becomes the
                // texture's name and its recipe path.
                return ProjectPaths::toProjectRelative(entry.path().string());
            }
        }
    }

    return std::nullopt;
}

TextureHandle loadOrFallback(
    const std::string& texturePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps,
    TextureHandle fallback
) {
    if (texturePath.empty()) {
        return fallback;
    }

    if (!std::filesystem::exists(ProjectPaths::resolveProjectPath(texturePath))) {
        LOG_WARNING("Texture file not found: '%s', using fallback", texturePath.c_str());
        return fallback;
    }

    // Folder-loaded materials no longer block on each texture decode.
    // requestTextureAsync returns immediately; GLMaterial::bindTextures
    // shows the 1x1 gray placeholder until pixels land 1-3 frames out.
    auto handle = requestTextureAsync(texturePath, resourceManager, srgb, generateMipmaps);
    if (!handle) {
        LOG_WARNING("Failed to request texture: '%s', using fallback", texturePath.c_str());
        return fallback;
    }

    return handle;
}

// Discover a folder's maps and assemble the material they describe.
//
// The pattern lists are the common naming conventions per map type. findTexture
// lowercases both the filename and each pattern before matching, so patterns are
// listed once in lowercase. Distinct spellings that are NOT mere case variants
// (e.g. "basecolor" vs "base_color") are kept - they match different filenames.
MaterialHandle buildFolderMaterial(const std::string& folderRef, ResourceManager& resourceManager) {
    const std::string albedoPath = findTexture(folderRef, {
        "color", "albedo", "basecolor", "diffuse", "base_color"
    }).value_or("");
    const std::string normalPath = findTexture(folderRef, {
        "normal", "normalgl", "normal_gl", "norm"
    }).value_or("");
    const std::string metallicPath = findTexture(folderRef, {
        "metallic", "metalness", "metal"
    }).value_or("");
    const std::string roughnessPath = findTexture(folderRef, {
        "roughness", "rough"
    }).value_or("");
    const std::string metallicRoughnessPath = findTexture(folderRef, {
        "metallicroughness", "metallic_roughness",
        "orm",  // Occlusion-Roughness-Metallic
        "rma"   // Roughness-Metallic-AO
    }).value_or("");
    const std::string aoPath = findTexture(folderRef, {
        "ao", "ambientocclusion", "ambient_occlusion", "occlusion"
    }).value_or("");
    const std::string emissionPath = findTexture(folderRef, {
        "emission", "emissive", "emit", "glow"
    }).value_or("");
    const std::string heightPath = findTexture(folderRef, {
        "height", "displacement", "disp", "parallax"
    }).value_or("");

    // Every scalar keeps MaterialAsset's own default except these two. The shader
    // multiplies each factor by its map, and a folder material always binds
    // something in both slots - the discovered map, or a constant fallback that
    // carries the default (white = fully rough, black = dielectric). So both
    // factors have to be 1: anything lower scales the authored map down as well
    // as the fallback, and metallic's old 0.0 erased a metallic or packed map
    // outright, rendering every such folder material as a dielectric.
    MaterialAsset material;
    material.metallic  = 1.0f;
    material.roughness = 1.0f;

    const TextureHandle whiteTex  = generateWhiteTexture(resourceManager);
    const TextureHandle blackTex  = generateBlackTexture(resourceManager);
    const TextureHandle normalTex = generateNormalTexture(resourceManager);

    material.albedoTexture = loadOrFallback(
        albedoPath,
        resourceManager,
        true,  // sRGB
        true,  // mipmaps
        whiteTex
    );

    material.normalTexture = loadOrFallback(
        normalPath,
        resourceManager,
        false,  // linear
        true,   // mipmaps
        normalTex
    );

    if (!metallicRoughnessPath.empty()) {
        // Packed glTF map (G = roughness, B = metallic). Bind it to the dedicated
        // packed slot so the shader samples the right channels; binding it to the
        // separate metallic/roughness slots reads everything from .r and corrupts
        // PBR. The MetallicRoughness texture-flag bit is derived from this handle.
        material.metallicRoughnessTexture = loadOrFallback(
            metallicRoughnessPath,
            resourceManager,
            false,  // linear
            true,   // mipmaps
            blackTex
        );
    } else {
        material.metallicTexture = loadOrFallback(
            metallicPath,
            resourceManager,
            false,  // linear
            true,   // mipmaps
            blackTex
        );
        material.roughnessTexture = loadOrFallback(
            roughnessPath,
            resourceManager,
            false,  // linear
            true,   // mipmaps
            whiteTex
        );
    }

    material.aoTexture = loadOrFallback(
        aoPath,
        resourceManager,
        false,  // linear
        true,   // mipmaps
        whiteTex
    );

    material.emissionTexture = loadOrFallback(
        emissionPath,
        resourceManager,
        true,  // sRGB (emission is a color)
        true,  // mipmaps
        blackTex
    );

    material.heightTexture = loadOrFallback(
        heightPath,
        resourceManager,
        false,     // linear (data texture)
        true,      // mipmaps
        blackTex   // Flat surface (no displacement)
    );

    LOG_VERBOSE("Material created: albedo=%s, normal=%s, metallic=%s, roughness=%s, ao=%s, emission=%s, height=%s",
        !albedoPath.empty() ? "custom" : "fallback",
        !normalPath.empty() ? "custom" : "fallback",
        !metallicPath.empty() ? "custom" : "fallback",
        !roughnessPath.empty() ? "custom" : "fallback",
        !aoPath.empty() ? "custom" : "fallback",
        !emissionPath.empty() ? "custom" : "fallback",
        !heightPath.empty() ? "custom" : "fallback"
    );

    return resourceManager.add(std::move(material));
}

} // namespace

MaterialHandle loadMaterialFromFolder(
    const std::string& folderPath,
    ResourceManager& resourceManager
) {
    // Idempotent by name like every other loader, and the folder reference is
    // this material's name: a second load of the same folder is that same
    // material, not a second copy of it living under a suffixed name. Relativised
    // before the lookup, or one folder named two ways becomes two materials.
    const std::string ref = ProjectPaths::toProjectRelative(folderPath);
    if (MaterialHandle loaded = resourceManager.findByName<MaterialAsset>(ref)) return loaded;

    if (!std::filesystem::exists(ProjectPaths::resolveProjectPath(ref))) {
        LOG_ERROR("Material folder not found: '%s'", ref.c_str());
        return MaterialHandle{};
    }

    LOG_INFO("Loading material from folder: '%s'", ref.c_str());

    MaterialHandle handle = buildFolderMaterial(ref, resourceManager);
    if (handle) {
        // Record how this material was created so SceneSerializer can recreate
        // it on a cold-start load (texture discovery happens again at reload).
        // The folder reference is the material's stable identity - it is also the
        // name scene refs resolve by, so rename to it (keeps the name index in
        // sync; edit().name would not).
        resourceManager.rename(handle, ref);
        resourceManager.edit(handle).sourceJson() = {
            {"kind", "folder"},
            {"path", ref}
        };
    }
    return handle;
}

} // namespace Vkm::Engine
