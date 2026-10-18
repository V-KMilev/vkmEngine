#pragma once

#include <cstdint>

#include "core/reflect.h"
#include "ecs/entity.h"
#include "resource/asset/terrain_asset.h"
#include "system/terrain/terrain_brush.h"

namespace Vkm::Engine {

struct EditorContext;

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
    EntityId    target{};            ///< The terrain edited: the last terrain entity selected.
    EntityId    followed{};          ///< The selection last seen; see followTerrainSelection.
    TerrainTool tool        = TerrainTool::None;
    float       radius      = 4.0f;  ///< Metres.
    float       strength    = 0.5f;  ///< 0 to 1: how fast a held brush works.
    float       hardness    = 0.3f;  ///< TerrainBrush::Dab::hardness.
    uint32_t    layer       = 0;     ///< What Paint paints.
    uint32_t    foliage     = 0;     ///< What Plants grows.
    bool        fixedHeight = false; ///< Flatten to height, not to where the stroke began.
    float       height      = 0.0f;  ///< Flatten's fixed height, local metres.
};

/// The smallest and largest brush, metres.
inline constexpr float TERRAIN_BRUSH_MIN_RADIUS = 0.25f;
inline constexpr float TERRAIN_BRUSH_MAX_RADIUS = 200.0f;

/**
 * @brief The terrain the brush edits, if it is still one: alive, with a Terrain and a Transform.
 *
 * @param ec The frame's editor context.
 * @return Its entity, or a null id.
 */
EntityId terrainToolTarget(const EditorContext& ec);

/**
 * @brief Whether the terrain tool has the viewport's left button: the Terrain window open with a
 *        tool picked, and a terrain to edit whose asset is loaded.
 *
 * @param ec The frame's editor context.
 * @return True while a drag would brush rather than move or pick.
 */
bool terrainToolApplies(const EditorContext& ec);

/**
 * @brief What the picked tool does under the left button, and what Shift does, in a few words.
 *
 * @param tool The tool.
 * @return A hint for a tooltip or the viewport's strip; empty for None.
 */
const char* terrainToolHint(TerrainTool tool);

/**
 * @brief The terrain brush in the viewport: its circle on the surface, the strip saying what a drag
 *        does, and the strokes a held left button makes, one undo step each.
 */
class TerrainToolOverlay {
    public:
        TerrainToolOverlay() = default;
        ~TerrainToolOverlay() = default;

        TerrainToolOverlay(const TerrainToolOverlay& other) = delete;
        TerrainToolOverlay& operator=(const TerrainToolOverlay& other) = delete;

        TerrainToolOverlay(TerrainToolOverlay && other) = delete;
        TerrainToolOverlay& operator=(TerrainToolOverlay && other) = delete;

    public:
        /**
         * @brief Draw the brush where the cursor meets the edited terrain, and stroke while the
         *        left button is held; Ctrl+click with Flatten takes the height clicked instead.
         *
         * @param ec The frame's editor context.
         * @return True when the brush took this frame's click, which then picks nothing.
         */
        bool update(EditorContext& ec);

        /**
         * @brief Whether a stroke is held: begun and its undo step not yet pushed.
         *
         * @return True from the press that began it to the release that ends it.
         */
        bool stroking() const { return static_cast<bool>(m_terrain); }

    private:
        /// Push the stroke's undo step, if it changed anything, and forget it.
        void finish(EditorContext& ec);

        /**
         * @brief The strip along the viewport's foot naming the tool, its size and its keys.
         *
         * @param ec The frame's editor context.
         */
        void drawStrip(const EditorContext& ec) const;

    private:
        TerrainHandle        m_terrain;    ///< The terrain being stroked; null between strokes.
        TerrainAsset         m_before;     ///< Its grid when the stroke began.
        TerrainBrush::Region m_touched;    ///< What the stroke has touched so far.
        float                m_flatten = 0.0f;  ///< Flatten's height, local.
        const char*          m_label   = "";    ///< The stroke's undo step.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::TerrainTool, "None", "Raise", "Lower", "Smooth", "Flatten", "Paint", "Plants")
