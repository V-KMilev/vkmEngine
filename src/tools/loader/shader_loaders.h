#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "loader/shader_preprocessor.h"
#include "resource/asset_database.h"
#include "resource/resource_manager.h"
#include "resource/shader_asset.h"
#include "system/io/file_watcher.h"

namespace Engine {

/**
 * @brief Build and register a shader asset in one call.
 *
 * The `name` is the lookup key (typical convention: "shader:<subdir>").
 * `samplerBindings` survives every recompile (incl. hot reload) - they
 * also get mirrored into the source descriptor so a serialize ->
 * cold-start load round-trip preserves them.
 */
inline ShaderHandle loadShader(
    ResourceManager& resources,
    const std::string& path,
    const std::string& name,
    std::unordered_map<std::string, int> samplerBindings = {},
    bool variantAware = false)
{
    ShaderAsset asset;
    asset.path            = path;
    asset.samplerBindings = std::move(samplerBindings);
    asset.variantAware    = variantAware;
    asset.assetId         = AssetDatabase::get().registerOrGet(path, AssetKind::Shader);
    asset.sourceJson() = {{"kind", "directory"}, {"path", path}};
    if (!asset.samplerBindings.empty()) {
        asset.sourceJson()["samplerBindings"] = asset.samplerBindings;
    }
    if (variantAware) {
        asset.sourceJson()["variantAware"] = true;
    }
    return resources.add(std::move(asset), name);
}

/**
 * @brief Hot-reload watch a shader: any file change in its directory
 *        bumps the asset's version, which the backend picks up next frame.
 *
 * Also discovers the set of directories that the shader's source pulls
 * in via `#include` and registers a watch on each of them, so editing a
 * shared helper (e.g. `shaders/_helpers/lighting.glsl`) hot-reloads all
 * shaders that include it - not just the helper's own directory.
 *
 * The include set is discovered once at registration time. Adding a
 * brand-new `#include` to a shader after startup requires a restart to
 * pick up edits to the newly-referenced file.
 */
inline void watchShader(FileWatcher& watcher, ResourceManager& resources, ShaderHandle handle) {
    const auto& asset = resources.get(handle);
    auto bumpVersion = [&resources, handle]() { resources.commit(handle); };
    watcher.watch(asset.path, bumpVersion);

    // Discover included directories by preprocessing each top-level shader
    // file (vertex/fragment/geometry). Source is thrown away; we only need
    // the side-effect set.
    namespace fs = std::filesystem;
    const fs::path dir(asset.path);
    const fs::path topFiles[] = {
        dir / "vertex.shader",
        dir / "fragment.shader",
        dir / "geometry.shader",
    };
    std::unordered_set<std::string> dirs;
    for (const auto& p : topFiles) {
        std::error_code ec;
        if (!fs::exists(p, ec)) continue;
        (void)preprocessShaderFile(p.string(), {}, dirs);
    }

    // Skip the shader's own directory - already watched above.
    std::error_code canonEc;
    const std::string ownDir = fs::weakly_canonical(dir, canonEc).string();
    for (const auto& d : dirs) {
        if (d == ownDir) continue;
        watcher.watch(d, bumpVersion);
    }
}

} // namespace Engine
