#pragma once

#include <functional>
#include <vector>

#include "ecs/entity.h"
#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

struct EditorContext;
struct EditorState;
class Scene;

/**
 * @brief The Terrain window: which terrain, the tools, the brush, the layers and plants, and its grid.
 *
 * The tools work in the viewport (TerrainToolOverlay) while the window is open. Settings edit the
 * asset, one undo step a gesture.
 */
class TerrainEditorPanel {
    public:
        TerrainEditorPanel() = default;
        ~TerrainEditorPanel() = default;

        TerrainEditorPanel(const TerrainEditorPanel& other) = delete;
        TerrainEditorPanel& operator=(const TerrainEditorPanel& other) = delete;

        TerrainEditorPanel(TerrainEditorPanel && other) = delete;
        TerrainEditorPanel& operator=(TerrainEditorPanel && other) = delete;

    public:
        /**
         * @brief Draw the window's contents into the caller's current region.
         *
         * @param ec The frame's editor context.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Changes to the edited terrain, applied to one copy of it once the window is drawn.
         */
        using Edits = std::vector<std::function<void(TerrainAsset&)>>;

        /**
         * @brief The window with no terrain to edit: what to do about it.
         *
         * @param ec The frame's editor context.
         */
        void drawEmptyState(EditorContext& ec);

        /**
         * @brief Which terrain: a chooser over the scene's terrains, and its grid in a line.
         *
         * @param ec The frame's editor context.
         * @param terrain The edited terrain's asset; null when its entity names none.
         */
        void drawTargetRow(EditorContext& ec, const TerrainAsset* terrain);

        /**
         * @brief The tools as tiles, the picked one lit; picked again, the brush goes down.
         *
         * @param ec The frame's editor context.
         */
        void drawTools(EditorContext& ec);

        /**
         * @brief Radius, strength and hardness, the brush's profile drawn under them, and what
         *        the tool picked takes besides.
         *
         * @param ec The frame's editor context.
         */
        void drawBrush(EditorContext& ec);

        /**
         * @brief The four layers as tiles of their maps; the picked one's material and tiling.
         *
         * @param ec The frame's editor context.
         * @param terrain The edited terrain's asset.
         * @param[out] edits Gains what was changed.
         */
        void drawLayers(EditorContext& ec, const TerrainAsset& terrain, Edits& edits);

        /**
         * @brief The plant layers as tiles, one more to add one; the picked one's parts and settings.
         *
         * @param ec The frame's editor context.
         * @param terrain The edited terrain's asset.
         * @param[out] edits Gains what was changed.
         */
        void drawPlants(EditorContext& ec, const TerrainAsset& terrain, Edits& edits);

        /**
         * @brief How big the terrain is and how fine its grid; a new grid is resampled from the old.
         *
         * @param terrain The edited terrain's asset.
         * @param[out] edits Gains what was changed.
         */
        void drawGrid(const TerrainAsset& terrain, Edits& edits);
};

} // namespace Vkm::Engine
