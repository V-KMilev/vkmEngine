#define VKM_LOG_CATEGORY "LOADER"

#include "loader/material_loaders.h"

#include <filesystem>
#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "resource/resource_manager.h"
#include "loader/texture_loaders.h"
#include "generator/texture_generators.h"

namespace Engine {

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

// Scans the folder for the first file whose name contains one of the patterns
// (case-insensitive) and carries one of the extensions. Pattern "Color" matches
// "PavingStones_Color.jpg", "brick_color.png", etc.
std::optional<std::string> findTexture(
    const std::string& folderPath,
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& extensions = {".jpg", ".jpeg", ".png", ".tga", ".bmp"}
) {
    // Check if folder exists
    if (!std::filesystem::exists(folderPath) || !std::filesystem::is_directory(folderPath)) {
        return std::nullopt;
    }

    // Iterate through all files in the folder
    for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::string extension = entry.path().extension().string();

        // Check if file has one of the target extensions (case-insensitive)
        std::string extensionLower = toLower(extension);
        bool hasValidExtension = false;
        for (const auto& ext : extensions) {
            if (extensionLower == toLower(ext)) {
                hasValidExtension = true;
                break;
            }
        }
        if (!hasValidExtension) continue;

        // Check if filename contains any of the patterns (case-insensitive)
        std::string filenameLower = toLower(filename);
        for (const auto& pattern : patterns) {
            std::string patternLower = toLower(pattern);

            if (nameMatchesPattern(filenameLower, patternLower)) {
                return entry.path().string();
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

    if (!std::filesystem::exists(texturePath)) {
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

// Single-user helper for loadMaterialFromFolder; defined below.
MaterialHandle loadMaterialFromDesc(
    const MaterialLoadDesc& desc,
    ResourceManager& resourceManager
);
} // namespace

MaterialHandle loadMaterialFromFolder(
    const std::string& folderPath,
    ResourceManager& resourceManager,
    const MaterialLoadDesc& baseProperties
) {
    if (!std::filesystem::exists(folderPath)) {
        LOG_ERROR("Material folder not found: '%s'", folderPath.c_str());
        return MaterialHandle{};
    }

    LOG_INFO("Loading material from folder: '%s'", folderPath.c_str());

    MaterialLoadDesc desc = baseProperties;

    // Common naming patterns per texture type. findTexture lowercases both the
    // filename and each pattern before matching, so patterns are listed once in
    // lowercase. Distinct spellings that are NOT mere case variants (e.g.
    // "basecolor" vs "base_color") are kept - they match different filenames.
    auto findAlbedo = findTexture(folderPath, {
        "color", "albedo", "basecolor", "diffuse", "base_color"
    });
    auto findNormal = findTexture(folderPath, {
        "normal", "normalgl", "normal_gl", "norm"
    });
    auto findMetallic = findTexture(folderPath, {
        "metallic", "metalness", "metal"
    });
    auto findRoughness = findTexture(folderPath, {
        "roughness", "rough"
    });
    auto findMetallicRoughness = findTexture(folderPath, {
        "metallicroughness", "metallic_roughness",
        "orm",  // Occlusion-Roughness-Metallic
        "rma"   // Roughness-Metallic-AO
    });
    auto findAO = findTexture(folderPath, {
        "ao", "ambientocclusion", "ambient_occlusion", "occlusion"
    });
    auto findEmission = findTexture(folderPath, {
        "emission", "emissive", "emit", "glow"
    });
    auto findHeight = findTexture(folderPath, {
        "height", "displacement", "disp", "parallax"
    });

    // Assign found textures
    if (findAlbedo) desc.albedoPath = *findAlbedo;
    if (findNormal) desc.normalPath = *findNormal;
    if (findMetallicRoughness) desc.metallicRoughnessPath = *findMetallicRoughness;
    if (findMetallic) desc.metallicPath = *findMetallic;
    if (findRoughness) desc.roughnessPath = *findRoughness;
    if (findAO) desc.aoPath = *findAO;
    if (findEmission) desc.emissionPath = *findEmission;
    if (findHeight) desc.heightPath = *findHeight;

    MaterialHandle handle = loadMaterialFromDesc(desc, resourceManager);
    if (handle) {
        // Record how this material was created so SceneSerializer can recreate
        // it on a cold-start load (texture discovery happens again at reload).
        // The folder path is the material's stable identity - it is also the
        // name scene refs resolve by, so rename to it (keeps the name index in
        // sync; edit().name would not).
        resourceManager.rename(handle, folderPath);
        resourceManager.edit(handle).sourceJson() = {
            {"kind", "folder"},
            {"path", folderPath}
        };
    }
    return handle;
}

namespace {
MaterialHandle loadMaterialFromDesc(
    const MaterialLoadDesc& desc,
    ResourceManager& resourceManager
) {
    MaterialAsset material;

    // Set base properties
    material.albedo = desc.albedo;
    material.emission = desc.emission;
    material.metallic = desc.metallic;
    material.roughness = desc.roughness;
    material.ao = desc.ao;
    material.normalScale = desc.normalScale;
    material.heightScale = desc.heightScale;

    // Generate fallback textures
    auto whiteTex = generateWhiteTexture(resourceManager);
    auto blackTex = generateBlackTexture(resourceManager);
    auto normalTex = generateNormalTexture(resourceManager);

    // Load or fallback for each texture
    // Albedo (sRGB)
    material.albedoTexture = loadOrFallback(
        desc.albedoPath,
        resourceManager,
        true,  // sRGB
        desc.generateMipmaps,
        whiteTex
    );

    // Normal map (linear)
    material.normalTexture = loadOrFallback(
        desc.normalPath,
        resourceManager,
        false,  // linear
        desc.generateMipmaps,
        normalTex
    );

    // Metallic and Roughness
    // Check if we have a combined texture first
    if (!desc.metallicRoughnessPath.empty()) {
        // Packed glTF map (G = roughness, B = metallic). Bind it to the dedicated
        // packed slot so the shader samples the right channels; binding it to the
        // separate metallic/roughness slots reads everything from .r and corrupts
        // PBR. The MetallicRoughness texture-flag bit is derived from this handle.
        material.metallicRoughnessTexture = loadOrFallback(
            desc.metallicRoughnessPath,
            resourceManager,
            false,  // linear
            desc.generateMipmaps,
            blackTex
        );
    } else {
        // Load separate textures
        material.metallicTexture = loadOrFallback(
            desc.metallicPath,
            resourceManager,
            false,  // linear
            desc.generateMipmaps,
            blackTex
        );
        material.roughnessTexture = loadOrFallback(
            desc.roughnessPath,
            resourceManager,
            false,  // linear
            desc.generateMipmaps,
            whiteTex
        );
    }

    // Ambient Occlusion (linear)
    material.aoTexture = loadOrFallback(
        desc.aoPath,
        resourceManager,
        false,  // linear
        desc.generateMipmaps,
        whiteTex
    );

    // Emission (sRGB)
    material.emissionTexture = loadOrFallback(
        desc.emissionPath,
        resourceManager,
        true,  // sRGB (emission is a color)
        desc.generateMipmaps,
        blackTex
    );

    // Height/Displacement (linear)
    material.heightTexture = loadOrFallback(
        desc.heightPath,
        resourceManager,
        false,  // linear (data texture)
        desc.generateMipmaps,
        blackTex  // Flat surface (no displacement)
    );

    LOG_VERBOSE("Material created: albedo=%s, normal=%s, metallic=%s, roughness=%s, ao=%s, emission=%s, height=%s",
        !desc.albedoPath.empty() ? "custom" : "fallback",
        !desc.normalPath.empty() ? "custom" : "fallback",
        !desc.metallicPath.empty() ? "custom" : "fallback",
        !desc.roughnessPath.empty() ? "custom" : "fallback",
        !desc.aoPath.empty() ? "custom" : "fallback",
        !desc.emissionPath.empty() ? "custom" : "fallback",
        !desc.heightPath.empty() ? "custom" : "fallback"
    );

    return resourceManager.add(std::move(material));
}
} // namespace

} // namespace Engine
