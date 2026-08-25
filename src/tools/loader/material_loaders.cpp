#define VKM_LOG_CATEGORY "LOADER"

#include "loader/material_loaders.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

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

// The file matching the earliest of @p patterns, among those carrying one of
// @p extensions: "color" finds "PavingStones_Color.jpg" or "brick_color.png".
//
// Patterns are tried outermost and the candidate list is sorted, so a folder
// holding both X_Color.png and X_BaseColor.png always yields the same map. Both
// orders would otherwise be directory_iterator's, which is unspecified - and the
// answer becomes the material's cooked recipe, so a filesystem's idea of order
// would be frozen into the version-controlled library.
std::optional<std::string> findTexture(
    const std::string& folderPath,
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& extensions = {".jpg", ".jpeg", ".png", ".tga", ".bmp"}
) {
    const std::filesystem::path folder = ProjectPaths::resolveProjectPath(folderPath);
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        const std::string extensionLower = toLower(entry.path().extension().string());
        for (const auto& ext : extensions) {
            if (extensionLower != toLower(ext)) continue;
            candidates.push_back(entry.path());
            break;
        }
    }
    std::sort(candidates.begin(), candidates.end());

    for (const auto& pattern : patterns) {
        const std::string patternLower = toLower(pattern);
        for (const std::filesystem::path& candidate : candidates) {
            if (!nameMatchesPattern(toLower(candidate.filename().string()), patternLower)) continue;
            // The reference, not the walked absolute: it becomes the texture's
            // name and its recipe path.
            return ProjectPaths::toProjectRelative(candidate.string());
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

    // The shader multiplies each factor by its map, and a folder material always
    // binds one - the discovered map, or a fallback carrying the default (white
    // = fully rough, black = dielectric) - so anything under 1 scales it down.
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
    // Relativised before the lookup, or one folder named two ways becomes two
    // materials.
    const std::string ref = ProjectPaths::toProjectRelative(folderPath);
    if (MaterialHandle loaded = resourceManager.findByName<MaterialAsset>(ref)) return loaded;

    if (!std::filesystem::exists(ProjectPaths::resolveProjectPath(ref))) {
        LOG_ERROR("Material folder not found: '%s'", ref.c_str());
        return MaterialHandle{};
    }

    LOG_INFO("Loading material from folder: '%s'", ref.c_str());

    MaterialHandle handle = buildFolderMaterial(ref, resourceManager);
    if (handle) {
        // The folder reference is this material's stable identity and the name
        // scene refs resolve by; the source is what a cold-start load rebuilds
        // it from, rediscovering the folder's textures as it goes.
        resourceManager.rename(handle, ref);
        resourceManager.edit(handle).sourceJson() = {
            {"kind", "folder"},
            {"path", ref}
        };
    }
    return handle;
}

} // namespace Vkm::Engine
