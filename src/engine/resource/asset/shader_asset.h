#pragma once

#include <string>

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
 * Hot reload: a FileWatcherSystem polls `path`'s contents and calls
 * `ResourceManager::commit(handle)` when source files change. The
 * backend's resource-sync sees the version bump and rebuilds its
 * GPU object - the asset itself never knows about reloads.
 */
struct ShaderAsset : public Resource {
    std::string path;  ///< Source directory.
};

using ShaderHandle = Handle<ShaderAsset>;

} // namespace Engine
