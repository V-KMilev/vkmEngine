#pragma once

#include "base/reflect/enum_names.h"
#include "base/reflect/reflect.h"
#include "overlays/gizmo/transform_gizmo.h"

namespace Vkm::Engine {

/**
 * @brief What a viewport drag does; Select draws no gizmo, so it is a tool and not a GizmoOperation.
 */
enum class EditorTool {
    Select,
    Translate,
    Rotate,
    Scale,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Which kinds of gizmo the viewport draws (View > Show); a hidden kind draws no pickable marker
 *        either.
 */
struct GizmoVisibility {
    bool lights    = true;
    bool cameras   = true;
    bool audio     = true;   ///< Sources and listeners.
    bool acoustics = false;  ///< The ear's rays off the colliders, and those to each sound.
    bool probes    = true;   ///< Reflection probes and irradiance volumes.
    bool effects   = true;   ///< Decals and particle emitters.
    bool colliders = false;  ///< Colliders and joints.
    bool contacts  = false;  ///< Where bodies touch, and how hard the solver pushes each.
    bool bounds    = false;  ///< Each drawn object's world box.
    bool skeletons = false;  ///< Posed rigs as bone segments.
};

/**
 * @brief The workspace a project was left in, kept in its editor_settings.json.
 *
 * Which panel groups show, each named for its first window (EditorSystem::drawPanels), the tool and
 * its space, and what the viewport marks; where each docks is ImGui's ini.
 */
struct Workspace {
    bool showHierarchy = true;
    bool showInspector = true;
    bool showAssets    = true;
    bool showTerrain   = false;  ///< The Terrain window, docked with the Inspector.

    EditorTool      tool      = EditorTool::Translate;
    GizmoMode       gizmoMode = GizmoMode::Local;
    GizmoVisibility gizmos;  ///< What the viewport draws over the scene.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::EditorTool, "Select", "Translate", "Rotate", "Scale")

VKM_REFLECT_BEGIN(::Vkm::Engine::GizmoVisibility)
    VKM_F(lights)
    VKM_F(cameras)
    VKM_F(audio)
    VKM_F(acoustics)
    VKM_F(probes)
    VKM_F(effects)
    VKM_F(colliders)
    VKM_F(contacts)
    VKM_F(bounds)
    VKM_F(skeletons)
VKM_REFLECT_END()


VKM_REFLECT_BEGIN(::Vkm::Engine::Workspace)
    VKM_F(showHierarchy)
    VKM_F(showInspector)
    VKM_F(showAssets)
    VKM_F(showTerrain)
    VKM_F(tool)
    VKM_F(gizmoMode)
    VKM_F(gizmos)
VKM_REFLECT_END()
