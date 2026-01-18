#include "material_generators.h"

#include "logger.h"
#include "resource_manager.h"
#include "texture_generators.h"

namespace Engine {

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    MaterialAsset material;

    // Basic white material with neutral PBR properties
    material.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    material.roughness = 0.5f;
    material.metallic = 0.0f;
    material.ao = 1.0f;
    material.emission = glm::vec3(0.0f);

    // Generate and assign default textures
    material.albedoTexture = generateWhiteTexture(resourceManager);
    material.normalTexture = generateNormalTexture(resourceManager);
    material.roughnessTexture = generateGrayTexture(resourceManager);
    material.metallicTexture = generateBlackTexture(resourceManager);
    material.aoTexture = generateWhiteTexture(resourceManager);
    material.emissionTexture = generateBlackTexture(resourceManager);

    auto handle = resourceManager.add(std::move(material));
    LOG_TRACE("Generated default material (handle: %u)", handle.value);

    return handle;
}

} // namespace Engine
