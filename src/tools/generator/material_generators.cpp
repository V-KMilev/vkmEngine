#define VKM_LOG_CATEGORY "GENERATOR"

#include "generator/material_generators.h"

#include <nlohmann/json.hpp>

#include "logger.h"

#include "resource/resource_manager.h"
#include "generator/texture_generators.h"

namespace Engine {

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    MaterialAsset material;

    material.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    material.roughness = 0.5f;
    material.metallic = 0.0f;
    material.ao = 1.0f;
    material.emission = glm::vec3(0.0f);

    material.albedoTexture = generateWhiteTexture(resourceManager);
    material.normalTexture = generateNormalTexture(resourceManager);
    material.roughnessTexture = generateGrayTexture(resourceManager);
    material.metallicTexture = generateBlackTexture(resourceManager);
    material.aoTexture = generateWhiteTexture(resourceManager);
    material.emissionTexture = generateBlackTexture(resourceManager);

    material.name = "material:default";
    auto handle = resourceManager.add(std::move(material));
    // Stamp a source so SceneSerializer can recreate this on cold-start load.
    auto& asset = resourceManager.edit(handle);
    asset.sourceJson() = {{"kind", "default"}};
    LOG_TRACE("Generated default material (handle: %u)", handle.id());

    return handle;
}

} // namespace Engine
