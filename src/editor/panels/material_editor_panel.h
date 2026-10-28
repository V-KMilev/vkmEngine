#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"
#include "system/render/editor_render_hooks.h"
#include "ui/asset_picker.h"
#include "ui/preview_viewport.h"

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
 * @brief The plain grey a mesh previews in when no material of its own is asked for.
 *
 * Hidden, and looked up by name first, so its callers share one material.
 *
 * @param resources Where the material is registered, privately.
 * @return Its handle.
 */
MaterialHandle neutralPreviewMaterial(ResourceManager& resources);

/**
 * @brief One material's parameters beside a live 3D preview: what its own window (MaterialEditor)
 *        draws.
 *
 * Wide, the preview fills the left and the parameters scroll on the right; narrow, the preview
 * folds away above them. Each map sits on the row of the value it multiplies, a thumbnail that picks,
 * clears or generates it and takes a texture dragged from the Assets panel. The selection moves
 * nothing; choosing another material opens that one in a window of its own. Secondary lobes appear
 * once the material uses them.
 *
 * Every edit is live and undoable, and shows wherever the shared material is drawn. The
 * preview goes through MaterialPreviewSession and the backend's offscreen hooks.
 */
class MaterialEditorPanel {
    public:
        /**
         * @brief The editor of @p material.
         *
         * @param material   The material it edits.
         * @param previewKey Its preview's target key.
         */
        MaterialEditorPanel(MaterialHandle material, uint64_t previewKey);
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
         * @brief Draw the chooser row: which material, who uses it, what can be done to it.
         *
         * @param ec The frame's editor context.
         */
        void drawIdentityRow(EditorContext& ec);

        /**
         * @brief Draw the material chooser combo, showing the material edited.
         *
         * @param ec A pick opens that material in a window of its own.
         * @param width Width the combo fills.
         */
        void drawChooser(EditorContext& ec, float width);

        /**
         * @brief Draw the live preview, filling @p size, with its shape buttons over its foot.
         *
         * @param ec The frame's editor context.
         * @param entityMesh The selection's mesh for the "as selected" shape; null disables it.
         * @param size Its size in screen pixels; a component of 0 takes the room left.
         */
        void drawPreview(EditorContext& ec, MeshHandle entityMesh, glm::vec2 size);

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
         * @brief Draw every parameter card for the material.
         *
         * @param resources Resolves and edits texture slots.
         * @param hooks Resolves a slot's GPU texture for its tile.
         * @param mat The asset the controls write to.
         * @return Whether any field changed this frame.
         */
        bool drawParameters(ResourceManager& resources, EditorRenderHooks& hooks, MaterialAsset& mat);

        /**
         * @brief One map's thumbnail, a frame tall, to lead the row of the value it multiplies.
         *
         * A click picks a texture; a right click replaces, clears or generates one; a texture
         * dragged from the Assets panel binds where it is dropped.
         *
         * @param resources Resolves and edits texture slots.
         * @param hooks Resolves the slot's GPU texture for its thumbnail.
         * @param mat The asset the slot lives on.
         * @param label The map's name.
         * @param member The slot.
         * @param usage Decides how a bound texture is decoded.
         * @param hint Shown on hover.
         * @return Whether the slot changed this frame.
         */
        bool textureSlot(
            ResourceManager& resources,
            EditorRenderHooks& hooks,
            MaterialAsset& mat,
            const char* label,
            TextureHandle MaterialAsset::* member,
            TextureUsage usage,
            const char* hint
        );

        /**
         * @brief Arm the texture picker for one slot.
         *
         * @param member The slot to fill once a file is chosen.
         * @param usage Decides how the texture is decoded.
         */
        void openTexturePicker(TextureHandle MaterialAsset::* member, TextureUsage usage);

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
         * The selection takes it only when it draws with the material edited, as with Duplicate.
         *
         * @param ec Opens the new material.
         */
        void servicePbrFolder(EditorContext& ec);

    private:
        MaterialHandle m_material;  ///< What it edits.

        PreviewViewport m_viewport;     ///< The live preview: orbit, pan, zoom, backdrop and light.
        int             m_shape = 0;    ///< Into the preview shape table.
        bool            m_open  = true; ///< The preview, folded or not, while the window is narrow.

        // One per modal, so each cache survives the other's open/close.
        AssetPicker m_pbrFolderPicker;
        AssetPicker m_texturePicker;

        /**
         * @brief The slot the active texture picker is filling.
         *
         * A pointer-to-member, surviving reallocation. Null means none is armed.
         */
        TextureHandle MaterialAsset::*  m_pendingSlot = nullptr;
        TextureUsage                    m_pendingTextureUsage = TextureUsage::Data;

        /// Color edited in a tile's "solid color" generator.
        glm::vec4 m_genColor{1.0f, 1.0f, 1.0f, 1.0f};

        // Rename modal, armed from the actions menu.
        bool        m_renameOpen = false;
        char        m_renameBuf[128] = {};
        std::string m_renameOldName;

        char m_chooserFilter[48] = {};  ///< Chooser combo search

        /// The entities drawing the material shown (AssetSerializer::forEachMaterialOf), walked again
        /// only once it, the world, its assets or the history moved (m_usersKey), or every frame of
        /// a session, which edits outside the history.
        std::vector<EntityId>                  m_users;
        std::optional<std::array<uint64_t, 4>> m_usersKey;  ///< Empty until the first walk.
};

} // namespace Vkm::Engine
