#pragma once

#include <cstdint>

#include "base/reflect/enum_names.h"
#include "ecs/entity.h"
#include "resource/asset/terrain_asset.h"
#include "system/terrain/terrain_brush.h"

namespace Vkm::Engine {

struct EditorContext;
struct EditorState;
class Scene;

/**
 * @brief What a left drag over the edited terrain does, picked in the Terrain window.
 */
enum class TerrainTool : uint8_t {
    None,     ///< The viewport picks and moves as ever.
    Raise,    ///< Shift lowers.
    Lower,    ///< Shift raises.
    Smooth,
    Flatten,  ///< To the height where the stroke began, or TerrainToolSettings::height.
    Paint,    ///< The chosen layer.
    Plants,   ///< Grows the chosen plant layer; Shift thins it.
    Count     ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief The terrain brush as the Terrain window sets it, and the terrain it works on.
 */
struct TerrainToolSettings {
    EntityId    target{};             ///< The terrain edited: the last terrain entity selected.
    EntityId    followed{};           ///< The selection last seen; see followTerrainSelection.
    TerrainTool tool        = TerrainTool::None;
    float       radius      = 4.0f;   ///< Meters.
    float       strength    = 0.5f;   ///< 0 to 1: how fast a held brush works.
    float       hardness    = 0.3f;   ///< TerrainBrush::Dab::hardness.
    uint32_t    layer       = 0;      ///< What Paint paints.
    uint32_t    foliage     = 0;      ///< What Plants grows.
    bool        fixedHeight = false;  ///< Flatten to height, not to where the stroke began.
    float       height      = 0.0f;   ///< Flatten's fixed height, local meters.
};

inline constexpr float TERRAIN_BRUSH_MIN_RADIUS = 0.25f;   ///< The smallest brush's radius, meters.
inline constexpr float TERRAIN_BRUSH_MAX_RADIUS = 200.0f;  ///< The largest brush's radius, meters.

/**
 * @brief The terrain the brush edits, if still alive with a Terrain and a Transform.
 *
 * @param ec The frame's editor context.
 * @return Its entity, or a null id.
 */
EntityId terrainToolTarget(const EditorContext& ec);

/**
 * @brief Whether a left drag brushes: Terrain window open, a tool picked, a terrain with its asset loaded.
 *
 * @param ec The frame's editor context.
 * @return True while a drag would brush rather than move or pick.
 */
bool terrainToolApplies(const EditorContext& ec);

/**
 * @brief What a tool and Shift do under the left button, in a few words.
 *
 * @param tool Any tool, None included.
 * @return A hint for a tooltip or the viewport's strip; empty for None.
 */
const char* terrainToolHint(TerrainTool tool);

/**
 * @brief Point the terrain brush at the selection when the selection is a terrain.
 *
 * Call every frame, the window shown or not. It acts only when the selection changes, so a terrain
 * chosen in the window holds; selecting anything else leaves the brush where it was.
 *
 * @param state The selection and the brush.
 * @param scene Says whether the selection is a terrain.
 */
void followTerrainSelection(EditorState& state, const Scene& scene);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::TerrainTool, "None", "Raise", "Lower", "Smooth", "Flatten", "Paint", "Plants")
