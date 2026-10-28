#include "resource/generate/material_generators.h"

#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

// What water of either kind is, optically: its index, and the green-blue light scattered in it takes on.
MaterialAsset waterOptics(WaterKind kind) {
    MaterialAsset water;
    water.water           = kind;
    water.ior             = 1.33f;
    water.subsurface      = 0.6f;
    water.subsurfaceColor = glm::vec3(0.06f, 0.22f, 0.24f);
    return water;
}

} // namespace

MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager) {
    return resourceManager.findOrAdd<MaterialAsset>("material:default", [] { return MaterialAsset{}; });
}

MaterialHandle generateWaterMaterial(ResourceManager& resourceManager) {
    return resourceManager.findOrAdd<MaterialAsset>("material:water", [] {
        MaterialAsset water = waterOptics(WaterKind::Body);
        water.albedo        = glm::vec4(0.02f, 0.05f, 0.06f, 1.0f);
        water.roughness     = 0.04f;
        return water;
    });
}

MaterialHandle generateSeaMaterial(ResourceManager& resourceManager) {
    return resourceManager.findOrAdd<MaterialAsset>("material:sea", [] {
        MaterialAsset sea = waterOptics(WaterKind::Sea);
        sea.albedo        = glm::vec4(0.01f, 0.03f, 0.045f, 1.0f);
        sea.roughness     = 0.06f;
        sea.rippleScale   = 0.5f;
        return sea;
    });
}

} // namespace Vkm::Engine
