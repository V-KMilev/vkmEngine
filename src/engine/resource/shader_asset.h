#pragma once

#include <string>
#include <unordered_map>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief A shader program described as a directory of source files.
 *
 * The directory contains `vertex.shader`, `fragment.shader`,
 * and optionally `geometry.shader` - same convention as
 * Core::Shader. The backend (GLShader, future VkShader, etc.) reads
 * this asset to produce its concrete program object.
 *
 * `samplerBindings` maps shader sampler-uniform names to the texture
 * unit slot they should bind to. Backends apply these after every
 * (re)compile so a hot reload doesn't reset texture bindings.
 *
 * Hot reload: a FileWatcher polls `path`'s contents and calls
 * `ResourceManager::commit(handle)` when source files change. The
 * backend's resource-sync sees the version bump and rebuilds its
 * GPU object - the asset itself never knows about reloads.
 */
struct ShaderAsset : public Resource {
    std::string path;  ///< Source directory.

    /// uniform-name -> texture slot. Applied after every compile by
    /// the backend so the binding survives hot reload.
    std::unordered_map<std::string, int> samplerBindings;

    /**
     * @brief Whether this shader's source uses material-feature #defines and
     *
     * should therefore be compiled per-material through the variant
     * cache. Default false: the shader is a single program reused by
     * every material that references it (the right choice for unlit,
     * depth-only, and every post-processing pass). Set to true for
     * shaders whose source has `#ifdef HAS_TRANSMISSION` / `_CLEARCOAT`
     * / ... blocks (today: pbr).
     */
    bool variantAware = false;
};

using ShaderHandle = Handle<ShaderAsset>;

} // namespace Engine
