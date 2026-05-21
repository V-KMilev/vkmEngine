#pragma once

#include "framework/editor_panel.h"
#include "resource/mesh_asset.h"

namespace Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief Floating Material Editor window with a live 3D preview.
 *
 * Opened from the Inspector ("Edit Material") or Window > Material Editor
 * (toggles EditorState::showMaterialEditor; the title-bar X clears it).
 * Edits the targeted MaterialAsset live - changes show instantly in the
 * preview and everywhere the material is used (materials are shared by
 * handle). "Duplicate" forks the material so a copy can be tweaked safely.
 *
 * The 3D preview is rendered by the backend (RenderBackend::
 * renderMaterialPreview) under a fixed studio camera + key light + the
 * scene's baked IBL, and shown via ImGui::Image - the editor never touches
 * GL itself.
 */
class MaterialEditorPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "MaterialEditor"; }
        void draw(EditorContext& ec) override;

    private:
        /// Lazily register the built-in preview shapes as real MeshAssets
        /// (one-time) and return the handle for the current selection.
        MeshHandle previewMesh(ResourceManager& resources, const MeshHandle& entityMesh);

        // Orbit/zoom state for the preview camera.
        float m_yaw      = 35.0f;
        float m_pitch    = 20.0f;
        float m_distance = 3.0f;
        int   m_primitive = 0;     ///< 0 sphere, 1 cube, 2 plane, 3 entity mesh
};

} // namespace Engine
