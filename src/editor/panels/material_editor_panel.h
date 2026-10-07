#pragma once

#include <string>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"
#include "system/render/editor_render_hooks.h"
#include "ui/asset_picker.h"

namespace Vkm::Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief The sphere material previews are drawn on, generated on first request.
 *
 * Hidden, and looked up by name first, so its callers share one mesh.
 *
 * @param resources Where the mesh is registered, privately.
 * @return Its handle.
 */
MeshHandle materialPreviewSphere(ResourceManager& resources);

/**
 * @brief The Material tab: one material's parameters under a live 3D preview.
 *
 * Draws into the Material window EditorSystem begins. The target follows the selection,
 * but one chosen by hand holds until another entity with a material is picked, so an
 * unused material can be worked on. Secondary lobes appear once the material uses them.
 *
 * Every edit is live and undoable, and shows wherever the shared material is drawn. The
 * preview goes through MaterialPreviewSession and the backend's offscreen hooks.
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
        /**
         * @brief Draw the whole panel into the caller's current region.
         *
         * @param ec The frame's editor context.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Settle which material this frame edits, and drop a dead pin.
         *
         * The pin records the selection it was made against: a hidden docked tab does not see
         * the selection change.
         *
         * @param ec Supplies the selection and pin; the pin clears when another entity with a
         *        material is selected.
         * @return The material to edit, or a null handle when there is none.
         */
        MaterialHandle resolveTarget(EditorContext& ec);

        /**
         * @brief Draw the panel with no material to edit: the two ways to get one.
         *
         * @param ec The frame's editor context.
         */
        void drawEmptyState(EditorContext& ec);

        /**
         * @brief Draw the chooser row: which material, who uses it, what can be done to it.
         *
         * @param ec The frame's editor context.
         * @param target The material being edited.
         */
        void drawIdentityRow(EditorContext& ec, MaterialHandle target);

        /**
         * @brief Draw the material chooser combo.
         *
         * @param ec A pick writes its target.
         * @param target Shown in the combo.
         * @param width Width the combo fills.
         */
        void drawChooser(EditorContext& ec, MaterialHandle target, float width);

        /**
         * @brief Draw the live preview, its shape and background controls and the light dial.
         *
         * @param ec The frame's editor context.
         * @param target The material being previewed.
         * @param entityMesh The selection's mesh for the "as selected" shape; null disables it.
         */
        void drawPreview(EditorContext& ec, MaterialHandle target, MeshHandle entityMesh);

        /**
         * @brief Resolve the preview shape to a real MeshAsset handle.
         *
         * Looked up by name every call and re-registered if absent, surviving a
         * ResourceManager::swap.
         *
         * @param resources Where the shapes are registered.
         * @param entityMesh Mesh for the "as selected" shape.
         * @return The shape to render the material on.
         */
        MeshHandle previewMesh(ResourceManager& resources, const MeshHandle& entityMesh);

        /**
         * @brief Draw every parameter card for one material.
         *
         * @param resources Resolves and edits texture slots.
         * @param backend Resolves a slot's GPU texture for its tile; may be null.
         * @param target The edited material, stable across slot reallocations.
         * @param mat The asset the controls write to.
         * @return Whether any field changed this frame.
         */
        bool drawParameters(
            ResourceManager& resources,
            EditorRenderHooks* backend,
            MaterialHandle target,
            MaterialAsset& mat
        );

        /**
         * @brief Draw the map grid: the core slots, whatever else is bound, and the tile that adds one.
         *
         * @param resources Resolves and edits texture slots.
         * @param backend Resolves a slot's GPU texture for its tile; may be null.
         * @param target The edited material.
         * @param mat The asset the tiles write to.
         * @return Whether any slot changed this frame.
         */
        bool drawMaps(
            ResourceManager& resources,
            EditorRenderHooks* backend,
            MaterialHandle target,
            MaterialAsset& mat
        );

        /**
         * @brief One map tile: the bound texture's thumbnail, its name, its file.
         *
         * Handle plus pointer-to-member, not a slot reference: storage can reallocate while
         * the deferred picker is open.
         *
         * @param resources Resolves and edits the slot.
         * @param backend Resolves the slot's GPU texture; null means no thumbnail.
         * @param owner The slot's material.
         * @param mat The asset being edited.
         * @param label The slot's name.
         * @param member The slot.
         * @param usage Decides how a bound texture is decoded.
         * @param hint Shown on hover.
         * @param face Tile edge length in pixels.
         * @return Whether the slot changed this frame.
         */
        bool mapTile(
            ResourceManager& resources,
            EditorRenderHooks* backend,
            MaterialHandle owner,
            MaterialAsset& mat,
            const char* label,
            TextureHandle MaterialAsset::* member,
            TextureUsage usage,
            const char* hint,
            float face
        );

        /**
         * @brief Arm the texture picker for one slot.
         *
         * @param owner The slot's material.
         * @param member The slot to fill once a file is chosen.
         * @param usage Decides how the texture is decoded.
         */
        void openTexturePicker(
            MaterialHandle owner,
            TextureHandle MaterialAsset::* member,
            TextureUsage usage
        );

        /**
         * @brief Bind whatever the texture picker returned, as an undo step.
         *
         * @param ec The frame's editor context.
         */
        void serviceTexturePicker(EditorContext& ec);

        /**
         * @brief Configure the folder picker and raise it.
         */
        void openPbrFolder();

        /**
         * @brief Run the "Load PBR Folder" picker and adopt the material it builds.
         *
         * The selection takes it only when it draws with @p target, as with Duplicate.
         *
         * @param ec Its target follows the new material.
         * @param target The material the tab showed when the picker finished.
         */
        void servicePbrFolder(EditorContext& ec, MaterialHandle target);

    private:
        // Preview camera orbit and zoom.
        float             m_yaw        = 35.0f;
        float             m_pitch      = 20.0f;
        float             m_distance   = 3.0f;
        int               m_shape      = 0;     ///< Into the preview shape table
        PreviewBackground m_background = PreviewBackground::Dark;  ///< Preview backdrop.
        float             m_lightYaw   = 0.0f;  ///< Preview light yaw, degrees

        // One per modal, so each cache survives the other's open/close.
        AssetPicker m_pbrFolderPicker;
        AssetPicker m_texturePicker;

        /**
         * @brief The material and slot the active texture picker is editing.
         *
         * Handle plus pointer-to-member, surviving reallocation. A null m_pendingSlot means
         * none is armed.
         */
        MaterialHandle                  m_pendingMaterial{};
        TextureHandle MaterialAsset::*  m_pendingSlot = nullptr;
        TextureUsage                    m_pendingTextureUsage = TextureUsage::Data;

        /// Colour edited in a tile's "solid colour" generator.
        glm::vec4 m_genColor{1.0f, 1.0f, 1.0f, 1.0f};

        // Rename modal, armed from the actions menu.
        bool        m_renameOpen = false;
        char        m_renameBuf[128] = {};
        std::string m_renameOldName;

        char m_chooserFilter[48] = {};  ///< Chooser combo search
};

} // namespace Vkm::Engine
