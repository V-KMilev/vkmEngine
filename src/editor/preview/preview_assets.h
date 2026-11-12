#pragma once

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

class ResourceManager;

inline constexpr const char* PREVIEW_SPHERE = "mesh:preview_sphere";  ///< The name the sphere material
                                                                      ///< previews draw on is registered
                                                                      ///< under, privately.

/**
 * @brief The hidden sphere material previews draw on, registered on the first call.
 *
 * @param resources Where the mesh is registered, privately.
 * @return The one mesh every caller shares.
 */
MeshHandle materialPreviewSphere(ResourceManager& resources);

/**
 * @brief The hidden plain gray a mesh previews in, registered on the first call.
 *
 * @param resources Where the material is registered, privately.
 * @return The one material every caller shares.
 */
MaterialHandle neutralPreviewMaterial(ResourceManager& resources);

} // namespace Vkm::Engine
