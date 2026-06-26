#pragma once

#include <glm/glm.hpp>

#include "resource/asset/material_asset.h"  // MaterialAsset, MaterialHandle
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
 * The 3D preview goes through MaterialPreviewSession (which renders via the
 * backend's offscreen preview hooks) and is shown via ImGui::Image - the
 * editor never touches GL itself.
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
        /**
         * @brief Resolve the preview shape for the current selection to a real
         * MeshAsset handle. Looks the shape up by name every call (O(1)) and
         * lazily re-registers it if absent, so it survives the ResourceManager
         * swap a scene load performs (which drops every hidden asset).
         */
        MeshHandle previewMesh(ResourceManager& resources, const MeshHandle& entityMesh);

        /**
         * @brief One material texture-slot row. Returns true on change. Takes the
         * owning material's handle + a pointer-to-member rather than a raw
         * slot reference so a deferred picker can re-resolve the slot safely
         * (the sparse-set backing can reallocate while the picker is open).
         */
        bool textureSlot(
            ResourceManager& res,
            const char* label,
            MaterialHandle owner,
            MaterialAsset& mat,
            TextureHandle MaterialAsset::* member,
            bool srgb
        );
        /**
         * @brief "Load PBR Folder" modal entry point; returns true once a folder
         * is picked, writing the absolute path to @p outFolder.
         */
        bool pbrFolderBrowse(std::string& outFolder);
        /**
         * @brief Draw the full PBR + texture editor body for one material.
         *
         * @param resources Resource manager used to resolve and edit texture slots.
         * @param target Handle of the material being edited (stable across slot reallocations).
         * @param mat The material asset whose fields the controls write to.
         * @return true if any field changed this frame.
         */
        bool drawMaterialBody(
            ResourceManager& resources,
            MaterialHandle target,
            MaterialAsset& mat
        );

        // Orbit/zoom state for the preview camera.
        float m_yaw      = 35.0f;
        float m_pitch    = 20.0f;
        float m_distance = 3.0f;
        int   m_primitive = 0;     ///< 0 sphere, 1 cube, 2 plane, 3 entity mesh

        // One picker per modal so each cache survives independent open/close.
        AssetPicker m_pbrFolderPicker;
        AssetPicker m_texturePicker;
        /**
         * @brief The material + slot the active texture picker is editing, identified
         * by handle + pointer-to-member (not a raw pointer) so it survives a
         * sparse-set reallocation. m_pendingSlot null == no picker active.
         */
        MaterialHandle                  m_pendingMaterial{};
        TextureHandle MaterialAsset::*  m_pendingSlot = nullptr;
        bool                            m_pendingTextureSrgb = false;

        /**
         * @brief Color edited in the per-slot "Gen" popup's solid-color generator.
         */
        glm::vec4 m_genColor{1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace Engine
