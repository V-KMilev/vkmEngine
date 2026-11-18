#include "preview/preview_assets.h"

#include <utility>

#include <glm/glm.hpp>

#include "io/asset/generate/mesh_generators.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

MeshHandle materialPreviewSphere(ResourceManager& resources) {
    MeshHandle h = resources.findByName<MeshAsset>(PREVIEW_SPHERE);
    if (!h) h = resources.addPrivate(generateSphere(), PREVIEW_SPHERE);
    return h;
}

MaterialHandle neutralPreviewMaterial(ResourceManager& resources) {
    const MaterialHandle found = resources.findByName<MaterialAsset>("mat:thumb_neutral");
    if (found) return found;
    MaterialAsset gray;
    gray.albedo    = glm::vec4(0.78f, 0.78f, 0.80f, 1.0f);
    gray.metallic  = 0.0f;
    gray.roughness = 0.55f;
    return resources.addPrivate(std::move(gray), "mat:thumb_neutral");
}

} // namespace Vkm::Engine
