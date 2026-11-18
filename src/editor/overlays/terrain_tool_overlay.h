#pragma once

#include "ecs/entity.h"
#include "resource/asset/terrain_asset.h"
#include "system/terrain/terrain_brush.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief The viewport's terrain brush: its circle, the strip naming the tool, strokes of one undo step each.
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
         * @brief Draw the brush and stroke while the left button is held; Ctrl+click Flatten picks a height.
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
        TerrainHandle        m_terrain;         ///< The terrain being stroked; null between strokes.
        TerrainAsset         m_before;          ///< Its grid when the stroke began.
        TerrainBrush::Region m_touched;         ///< What the stroke has touched so far.
        float                m_flatten = 0.0f;  ///< Flatten's height, local.
        const char*          m_label   = "";    ///< The stroke's undo step.
};

} // namespace Vkm::Engine
