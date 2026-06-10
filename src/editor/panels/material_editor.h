#pragma once

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"   // TextureHandle
#include "framework/asset_picker.h"

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
class MaterialEditorPanel {
    public:
        MaterialEditorPanel() = default;
        ~MaterialEditorPanel() = default;

        MaterialEditorPanel(const MaterialEditorPanel& other) = delete;
        MaterialEditorPanel& operator=(const MaterialEditorPanel& other) = delete;

        MaterialEditorPanel(MaterialEditorPanel && other) = delete;
        MaterialEditorPanel& operator=(MaterialEditorPanel && other) = delete;

    public:
        void draw(EditorContext& ec);

    private:
        /// Lazily register the built-in preview shapes as real MeshAssets
        /// (one-time) and return the handle for the current selection.
        MeshHandle previewMesh(ResourceManager& resources, const MeshHandle& entityMesh);

        /// One material texture-slot row. Returns true on change.
        bool textureSlot(
            ResourceManager& res,
            const char* label,
            TextureHandle& slot,
            bool srgb
        );
        /// "Load PBR Folder" modal entry point; returns true once a folder
        /// is picked, writing the absolute path to @p outFolder.
        bool pbrFolderBrowse(std::string& outFolder);
        /// The full PBR + texture editor body. Returns true if anything changed.
        bool drawMaterialBody(
            ResourceManager& resources,
            class MaterialAsset& mat
        );

        // Orbit/zoom state for the preview camera.
        float m_yaw      = 35.0f;
        float m_pitch    = 20.0f;
        float m_distance = 3.0f;
        int   m_primitive = 0;     ///< 0 sphere, 1 cube, 2 plane, 3 entity mesh

        // One picker per modal so each cache survives independent open/close.
        AssetPicker m_pbrFolderPicker;
        AssetPicker m_texturePicker;
        /// Which texture slot the current m_texturePicker session is editing.
        /// Null when no picker is active.
        TextureHandle* m_pendingTexture = nullptr;
        bool          m_pendingTextureSrgb = false;

        /// Color edited in the per-slot "Gen" popup's solid-color generator.
        glm::vec4 m_genColor{1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace Engine
