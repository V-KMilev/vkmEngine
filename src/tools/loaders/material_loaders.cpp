#include "material_loaders.h"

#include <filesystem>
#include <algorithm>

#include "logger.h"
#include "resource_manager.h"
#include "texture_loaders.h"
#include "texture_generators.h"

namespace Engine {

namespace {
    /**
     * @brief Convert string to lowercase for case-insensitive comparison.
     */
    std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), 
            [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    /**
     * @brief Search for a texture file matching common naming patterns.
     * 
     * Scans all files in the folder with the specified extensions and checks
     * if any filename contains one of the patterns (case-insensitive).
     * 
     * Example: Pattern "Color" matches "PavingStones_Color.jpg", "brick_color.png", etc.
     */
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
                
                if (filenameLower.find(patternLower) != std::string::npos) {
                    return entry.path().string();
                }
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Load a texture if path is provided, otherwise return fallback.
     */
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

        auto handle = loadTexture(texturePath, resourceManager, srgb, generateMipmaps);
        if (!handle) {
            LOG_WARNING("Failed to load texture: '%s', using fallback", texturePath.c_str());
            return fallback;
        }

        return handle;
    }
}

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

    // Common naming patterns for each texture type
    auto findAlbedo = findTexture(folderPath, {
        "Color", "color", "Albedo", "albedo", 
        "BaseColor", "basecolor", "Diffuse", "diffuse",
        "Base_Color", "base_color"
    });
    auto findNormal = findTexture(folderPath, {
        "Normal", "normal", "NormalGL", "normalgl",
        "Normal_GL", "normal_gl", "Norm", "norm"
    });
    auto findMetallic = findTexture(folderPath, {
        "Metallic", "metallic", "Metalness", "metalness",
        "Metal", "metal"
    });
    auto findRoughness = findTexture(folderPath, {
        "Roughness", "roughness", "Rough", "rough"
    });
    auto findMetallicRoughness = findTexture(folderPath, {
        "MetallicRoughness", "metallic_roughness",
        "ORM", "orm",  // Occlusion-Roughness-Metallic
        "RMA", "rma"   // Roughness-Metallic-AO
    });
    auto findAO = findTexture(folderPath, {
        "AO", "ao", "AmbientOcclusion", "ambient_occlusion",
        "Occlusion", "occlusion", "Ambient_Occlusion"
    });
    auto findEmission = findTexture(folderPath, {
        "Emission", "emission", "Emissive", "emissive",
        "Emit", "emit", "Glow", "glow"
    });
    auto findHeight = findTexture(folderPath, {
        "Height", "height", "Displacement", "displacement",
        "Disp", "disp", "Parallax", "parallax"
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

    return loadMaterialFromDesc(desc, resourceManager);
}

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
        auto combinedTex = loadOrFallback(
            desc.metallicRoughnessPath,
            resourceManager,
            false,  // linear
            desc.generateMipmaps,
            blackTex
        );
        material.metallicTexture = combinedTex;
        material.roughnessTexture = combinedTex;
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

    LOG_INFO("Material created: albedo=%s, normal=%s, metallic=%s, roughness=%s, ao=%s, emission=%s, height=%s",
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

MaterialHandle loadSimpleMaterial(
    const std::string& albedoPath,
    ResourceManager& resourceManager,
    float roughness,
    bool generateMipmaps
) {
    MaterialLoadDesc desc;
    desc.albedoPath = albedoPath;
    desc.roughness = roughness;
    desc.metallic = 0.0f;  // Non-metallic by default
    desc.generateMipmaps = generateMipmaps;

    return loadMaterialFromDesc(desc, resourceManager);
}

} // namespace Engine
