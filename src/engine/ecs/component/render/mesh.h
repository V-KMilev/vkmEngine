#pragma once

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

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

    bool movable = true;  ///< Off: it never moves, changes or hides while the world runs, so the renderer
                          ///< keeps its matrix, box and draw from the first frame it sees it; no material is
                          ///< read, so editing one keeps it. It checks a share of what it kept each frame and
                          ///< rebuilds, with a warning, if one did move. A host that may edit the world keeps
                          ///< nothing (HostChrome::authoring).
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::SeenBy, "All", "AllButEye", "EyeOnly", "Viewmodel")

VKM_REFLECT_BEGIN(::Vkm::Engine::Mesh)
    VKM_F(mesh)
    VKM_F(material)
    VKM_F(visible, "Off hides it, in the game as well")
    VKM_F(castShadows)
    VKM_F(seenBy, "Who draws it through a scene camera; the editor's view draws all")
    VKM_F(movable, "Off for scenery that never moves: the renderer keeps its work")
VKM_REFLECT_END()
