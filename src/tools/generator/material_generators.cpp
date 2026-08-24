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

    material.name = "material:default";
    auto handle = resourceManager.add(std::move(material));
    // Stamp a source so SceneSerializer can recreate this on cold-start load.
    auto& asset = resourceManager.edit(handle);
    asset.sourceJson() = {{"kind", "default"}};
    LOG_TRACE("Generated default material (handle: %u)", handle.id());

    return handle;
}

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    // One asset per name, the way the built-in textures this material binds are
    // already shared. Without it every caller added another material called
    // "material:default", ensureUniqueName suffixed it, and the suffix - names
    // being the serializable identity - became the frozen identity of whatever
    // it was attached to, in the scene file and in the cooked library. Six
    // primitives created from the menu left six identical materials behind,
    // numbered (2) through (7), and editing "the default material" reached
    // exactly one of them.
    if (auto existing = resourceManager.findByName<MaterialAsset>("material:default")) {
        return existing;
    }
    return buildDefaultMaterial(resourceManager);
}

} // namespace Vkm::Engine
