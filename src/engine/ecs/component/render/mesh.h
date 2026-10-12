#pragma once

#include "core/reflect.h"

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief Who draws a mesh: the camera the frame is seen through (the eye), and the rest -
 *        shadows and captures.
 *
 * Only a scene camera is an eye: an editor's free view draws everything.
 */
enum class SeenBy {
    All       = 0,  ///< Everything: the eye, shadows and captures.
    AllButEye = 1,  ///< A first-person body: casts its shadow, in probe captures, not in the eye.
    EyeOnly   = 2,  ///< Arms or a weapon: in the eye alone, with no shadow and in no capture.

    /**
     * @brief EyeOnly, drawn through the camera's viewmodel projection and in front of the world.
     *
     * Its own field of view (Camera::viewmodelFovY), and it never goes into a wall it is held
     * against. An editor's free view draws it where it is, as any mesh.
     */
    Viewmodel = 3,
    Count           ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 */
struct Mesh {
    MeshHandle     mesh;
    MaterialHandle material;
    bool           visible     = true;
    bool           castShadows = true;
    SeenBy         seenBy      = SeenBy::All;
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::SeenBy, "All", "AllButEye", "EyeOnly", "Viewmodel")

VKM_REFLECT_BEGIN(::Vkm::Engine::Mesh)
    VKM_F(mesh)
    VKM_F(material)
    VKM_F(visible)
    VKM_F(castShadows)
    VKM_F(seenBy)
VKM_REFLECT_END()
