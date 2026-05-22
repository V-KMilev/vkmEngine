#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

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
 */
inline void watchShader(FileWatcher& watcher, ResourceManager& resources, ShaderHandle handle) {
    const auto& asset = resources.get(handle);
    watcher.watch(asset.path, [&resources, handle]() { resources.commit(handle); });
}

} // namespace Engine
