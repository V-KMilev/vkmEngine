#define VKM_LOG_CATEGORY "LOADER"

#include "import/material_loaders.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "logger.h"

#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "import/texture_loaders.h"
#include "resource/generate/texture_generators.h"

namespace Vkm::Engine {

namespace {

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c) { return std::tolower(c); }
    );
    return result;
}

/**
 * @brief Does @p filename contain @p pattern as a whole word?
 *
 * Short patterns (acronyms) need token boundaries: "orm" and "rma" are inside *normal*. Longer
 * ones match as substrings, so "normalmap" still holds "normal".
 */
bool nameMatchesPattern(const std::string& filename, const std::string& pattern) {
    constexpr size_t ACRONYM_MAX = 3;
    if (pattern.size() > ACRONYM_MAX) return filename.find(pattern) != std::string::npos;

    const auto isWordChar = [](unsigned char c) { return std::isalnum(c) != 0; };
    for (size_t at = filename.find(pattern); at != std::string::npos; at = filename.find(pattern, at + 1)) {
        const bool leftOk  = at == 0 || !isWordChar(static_cast<unsigned char>(filename[at - 1]));
        const size_t after = at + pattern.size();
        const bool rightOk = after >= filename.size()
            || !isWordChar(static_cast<unsigned char>(filename[after]));
        if (leftOk && rightOk) return true;
    }
    return false;
}

// The file matching the earliest of @p patterns with one of @p extensions. Patterns outermost and
// candidates sorted, so the answer (frozen into the cooked recipe) never follows
// directory_iterator's unspecified order.
std::optional<std::string> findTexture(
    const std::string& folderPath,
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& extensions = {".jpg", ".jpeg", ".png", ".tga", ".bmp"}
) {
    namespace fs = std::filesystem;
    const fs::path folder = ProjectPaths::resolveProjectPath(folderPath);
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return std::nullopt;
    }

    // Stepped by hand: a directory unreadable mid-scan is no candidate, not an exception.
    std::vector<fs::path> candidates;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        // Not the walk's error_code: one unstattable entry would end the scan early.
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) continue;
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
            // The reference, not the absolute path: it becomes the name and recipe path.
            return ProjectPaths::toProjectRelative(candidate.string());
        }
    }

    return std::nullopt;
}

TextureHandle loadOrFallback(
    const std::string& texturePath,
    ResourceManager& resourceManager,
    TextureUsage usage,
    bool generateMipmaps,
    TextureHandle fallback
) {
    if (texturePath.empty()) {
        return fallback;
    }

    std::error_code ec;
    if (!std::filesystem::exists(ProjectPaths::resolveProjectPath(texturePath), ec)) {
        LOG_WARNING("Texture file not found: '%s', using fallback", texturePath.c_str());
        return fallback;
    }

    // Requested, not loaded, so a folder of maps does not block once per map.
    auto handle = requestTextureAsync(texturePath, resourceManager, usage, generateMipmaps);
    if (!handle) {
        LOG_WARNING("Failed to request texture: '%s', using fallback", texturePath.c_str());
        return fallback;
    }

    return handle;
}

// Assemble the material a folder's maps describe. Each list is lowercase, tried in order.
MaterialHandle buildFolderMaterial(const std::string& folderRef, ResourceManager& resourceManager) {
    const std::string albedoPath = findTexture(folderRef, {"color", "albedo", "diffuse"}).value_or("");
    const std::string normalPath = findTexture(folderRef, {"normal", "norm"}).value_or("");
    const std::string metallicPath = findTexture(folderRef, {"metallic", "metalness", "metal"}).value_or("");
    const std::string roughnessPath = findTexture(folderRef, {"roughness", "rough"}).value_or("");
    // Kept apart: ORM's red is occlusion, the glTF packing's is unused.
    // ORM: Occlusion-Roughness-Metallic.
    const std::string ormPath = findTexture(
        folderRef,
        {"orm", "occlusionroughnessmetallic", "occlusion_roughness_metallic"}
    ).value_or("");
    const std::string metallicRoughnessPath = findTexture(
        folderRef,
        {"metallicroughness", "metallic_roughness"}
    ).value_or("");
    // RMA (roughness, metalness, occlusion) has no slot and reads wrong in either packed one, so
    // it is only looked for to be named.
    if (ormPath.empty() && metallicRoughnessPath.empty()) {
        if (const auto rma = findTexture(folderRef, {"rma"})) {
            LOG_WARNING(
                "'%s' is packed RMA, which this engine does not read - it reads ORM and the glTF "
                "metallic-roughness layout. The material is built without it; repack it as ORM or "
                "split the channels.",
                rma->c_str()
            );
        }
    }
    const std::string aoPath = findTexture(
        folderRef,
        {"ao", "ambientocclusion", "ambient_occlusion", "occlusion"}
    ).value_or("");
    const std::string emissionPath = findTexture(
        folderRef,
        {"emission", "emissive", "emit", "glow"}
    ).value_or("");
    const std::string heightPath = findTexture(
        folderRef,
        {"height", "displacement", "disp", "parallax"}
    ).value_or("");

    // The shader multiplies each factor by its map, and a folder material always binds one (or a
    // fallback carrying the default), so anything under 1 scales it down.
    MaterialAsset material;
    material.metallic  = 1.0f;
    material.roughness = 1.0f;

    const bool generateMipmaps = true;
    const TextureHandle whiteTex  = generateWhiteTexture(resourceManager);
    const TextureHandle blackTex  = generateBlackTexture(resourceManager);
    const TextureHandle normalTex = generateNormalTexture(resourceManager);

    material.albedoTexture = loadOrFallback(
        albedoPath,
        resourceManager,
        TextureUsage::Color,
        generateMipmaps,
        whiteTex
    );

    material.normalTexture = loadOrFallback(
        normalPath,
        resourceManager,
        TextureUsage::Normal,
        generateMipmaps,
        normalTex
    );

    // No fallback: with metallic and roughness at 1, a failed packed map must fall through to the
    // separate maps, not leave raw metal.
    const TextureHandle ormTex = ormPath.empty()
        ? TextureHandle{}
        : loadOrFallback(ormPath, resourceManager, TextureUsage::Data, true, TextureHandle{});
    const TextureHandle mrTex = (ormTex || metallicRoughnessPath.empty())
        ? TextureHandle{}
        : loadOrFallback(metallicRoughnessPath, resourceManager, TextureUsage::Data, true, TextureHandle{});

    if (ormTex) {
        // R = occlusion, G = roughness, B = metallic; the separate AO map is left off below.
        material.aoMetallicRoughnessTexture = ormTex;
    } else if (mrTex) {
        // G = roughness, B = metallic: the separate slots read everything from .r.
        material.metallicRoughnessTexture = mrTex;
    } else {
        material.metallicTexture = loadOrFallback(
            metallicPath,
            resourceManager,
            TextureUsage::Data,
            generateMipmaps,
            blackTex
        );
        material.roughnessTexture = loadOrFallback(
            roughnessPath,
            resourceManager,
            TextureUsage::Data,
            generateMipmaps,
            whiteTex
        );
    }

    if (!ormTex) {
        material.aoTexture = loadOrFallback(
            aoPath,
            resourceManager,
            TextureUsage::Data,
            generateMipmaps,
            whiteTex
        );
    }

    material.emissionTexture = loadOrFallback(
        emissionPath,
        resourceManager,
        TextureUsage::Color,
        generateMipmaps,
        blackTex
    );

    material.heightTexture = loadOrFallback(
        heightPath,
        resourceManager,
        TextureUsage::Data,
        generateMipmaps,
        blackTex
    );

    // Names the metal/rough shape taken, or a packed map reads as "metallic = fallback".
    const char* metalRough = ormTex ? "orm"
        : mrTex ? "packed"
        : (!metallicPath.empty() || !roughnessPath.empty()) ? "separate"
        : "fallback";
    LOG_VERBOSE(
        "Material created: albedo=%s, normal=%s, metal/rough=%s, ao=%s, emission=%s, height=%s",
        !albedoPath.empty() ? "custom" : "fallback",
        !normalPath.empty() ? "custom" : "fallback",
        metalRough,
        ormTex ? "orm" : !aoPath.empty() ? "custom" : "fallback",
        !emissionPath.empty() ? "custom" : "fallback",
        !heightPath.empty() ? "custom" : "fallback"
    );

    return resourceManager.add(std::move(material));
}

} // namespace

MaterialHandle loadMaterialFromFolder(const std::string& folderPath, ResourceManager& resourceManager) {
    // Relativised first, or one folder named two ways becomes two materials.
    const std::string ref = ProjectPaths::toProjectRelative(folderPath);
    if (MaterialHandle loaded = resourceManager.findByName<MaterialAsset>(ref)) return loaded;

    std::error_code ec;
    if (!std::filesystem::exists(ProjectPaths::resolveProjectPath(ref), ec)) {
        LOG_ERROR("Material folder not found: '%s'", ref.c_str());
        return MaterialHandle{};
    }

    LOG_INFO("Loading material from folder: '%s'", ref.c_str());

    MaterialHandle handle = buildFolderMaterial(ref, resourceManager);
    // Named by the folder reference; a later load rebuilds from the cooked inline form, so no
    // folder recipe is kept.
    if (handle) resourceManager.rename(handle, ref);
    return handle;
}

} // namespace Vkm::Engine
