#pragma once

#include <string>
#include <unordered_map>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief A shader program described as a directory of source files.
 *
 * The directory contains `vertexShader.shader`, `fragmentShader.shader`,
 * and optionally `geometryShader.shader` — same convention as
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
 * GPU object — the asset itself never knows about reloads.
 */
struct ShaderAsset : public Resource {
    std::string path;  ///< Source directory.

    /// uniform-name -> texture slot. Applied after every compile by
    /// the backend so the binding survives hot reload.
    std::unordered_map<std::string, int> samplerBindings;
};

using ShaderHandle = Handle<ShaderAsset>;

} // namespace Engine
