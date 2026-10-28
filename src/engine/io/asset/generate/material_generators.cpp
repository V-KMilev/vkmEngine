#define VKM_LOG_CATEGORY "GENERATOR"

#include "resource/generate/material_generators.h"

#include "logger.h"

#include "resource/resource_manager.h"

namespace Vkm::Engine {

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    if (auto existing = resourceManager.findByName<MaterialAsset>("material:default")) {
        return existing;
    }

    const MaterialHandle handle = resourceManager.add(MaterialAsset{}, "material:default");
    LOG_TRACE("Generated default material (handle: %u)", handle.id());
    return handle;
}

} // namespace Vkm::Engine
