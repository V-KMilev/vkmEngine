#define VKM_LOG_CATEGORY "GENERATOR"

#include "generator/material_generators.h"

#include <nlohmann/json.hpp>

#include "logger.h"

#include "resource/resource_manager.h"
#include "generator/texture_generators.h"

namespace Vkm::Engine {

MaterialHandle buildDefaultMaterial(ResourceManager& resourceManager) {
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

    auto handle = resourceManager.add(std::move(material), "material:default");
    // Stamp a source so SceneSerializer can recreate this on cold-start load.
    auto& asset = resourceManager.edit(handle);
    asset.sourceJson() = {{"kind", "default"}};
    LOG_TRACE("Generated default material (handle: %u)", handle.id());

    return handle;
}

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    // One asset per name, the way the built-in textures this material binds are
    // already shared: a second "material:default" gets a unique-name suffix, and
    // that suffix - a name being the identity - freezes into every file naming it.
    if (auto existing = resourceManager.findByName<MaterialAsset>("material:default")) {
        return existing;
    }
    return buildDefaultMaterial(resourceManager);
}

} // namespace Vkm::Engine
