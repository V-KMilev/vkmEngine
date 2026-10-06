#define VKM_LOG_CATEGORY "GENERATOR"

#include "resource/generate/material_generators.h"

#include "logger.h"

#include "resource/resource_manager.h"
#include "resource/generate/texture_generators.h"

namespace Vkm::Engine {

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    if (auto existing = resourceManager.findByName<MaterialAsset>("material:default")) {
        return existing;
    }

    MaterialAsset material;
    material.albedoTexture = generateWhiteTexture(resourceManager);
    material.normalTexture = generateNormalTexture(resourceManager);
    material.roughnessTexture = generateGrayTexture(resourceManager);
    material.metallicTexture = generateBlackTexture(resourceManager);
    material.aoTexture = generateWhiteTexture(resourceManager);
    material.emissionTexture = generateBlackTexture(resourceManager);

    const MaterialHandle handle = resourceManager.add(std::move(material), "material:default");
    LOG_TRACE("Generated default material (handle: %u)", handle.id());
    return handle;
}

} // namespace Vkm::Engine
