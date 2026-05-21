#pragma once

#include "framework/editor_panel.h"
#include "resource/mesh_asset.h"
#include "resource/material_asset.h"

namespace Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief Floating Asset Browser: a live thumbnail grid of materials & meshes.
 *
 * Opened from View > Asset Browser. Each cell is a real-pipeline preview
 * (RenderSystem::materialPreviewTexture, budgeted so a big grid spreads its
 * bakes over several frames). Click a material to edit it (opens the Material
 * Editor); "Assign" applies a material/mesh to the selected entity's Mesh.
 * "Import Model..." reuses the existing model-import dialog.
 *
 * Stateless w.r.t. assets - it reads ResourceManager every frame; only the
 * cell size and the two cached helper assets (a preview sphere for material
 * thumbnails, a neutral material for mesh thumbnails) live here.
 */
class AssetBrowserPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "AssetBrowser"; }
        void draw(EditorContext& ec) override;

    private:
        void ensureAssets(ResourceManager& resources);
        void drawMaterials(EditorContext& ec);
        void drawMeshes(EditorContext& ec);

        float      m_cell = 104.0f;          ///< Thumbnail edge in px

        // Re-acquired every draw via ensureAssets - no ready flag so a
        // ResourceManager swap (scene load) doesn't leave stale handles.
        MeshHandle     m_sphere;             ///< Shape for material thumbnails
        MaterialHandle m_neutral;            ///< Material for mesh thumbnails
};

} // namespace Engine
