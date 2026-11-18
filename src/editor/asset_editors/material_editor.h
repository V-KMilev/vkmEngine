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
#include "ui/file_picker.h"
#include "preview/preview_viewport.h"

#include "asset_editors/asset_editor.h"

namespace Vkm::Engine {

/**
 * @brief A material in a window of its own, whatever is selected: its parameters beside a live 3D
 *        preview to orbit.
 *
 * The selection moves nothing; choosing another material opens it in a window of its own. Every
 * edit is live and undoable, and shows wherever the shared material is drawn.
 */
class MaterialEditor : public AssetEditor {
    public:
        /**
         * @brief An editor of the material at @p key.
         *
         * @param key Its handle's slot and generation.
         */
        explicit MaterialEditor(StorageIndex key);
        ~MaterialEditor() override = default;

        MaterialEditor(const MaterialEditor& other) = delete;
        MaterialEditor& operator=(const MaterialEditor& other) = delete;

        MaterialEditor(MaterialEditor && other) = delete;
        MaterialEditor& operator=(MaterialEditor && other) = delete;

    public:
        const Resource* asset(const ResourceManager& resources) const override;
        void            draw(EditorContext& ec) override;

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
         * Looked up by name every call and registered again if absent, so it survives a
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

        /// Configure the folder picker and raise it.
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

        PreviewViewport m_viewport;      ///< The live preview: orbit, pan, zoom, backdrop and light.
        int             m_shape = 0;     ///< Into the preview shape table.
        bool            m_open  = true;  ///< The preview, folded or not, while the window is narrow.

        // One per modal, so each cache survives the other's open/close.
        FilePicker m_pbrFolderPicker;
        FilePicker m_texturePicker;

        TextureHandle MaterialAsset::* m_pendingSlot         = nullptr;             ///< The slot the texture
                                                                                    ///< picker fills; null
                                                                                    ///< while none is armed.
        TextureUsage                   m_pendingTextureUsage = TextureUsage::Data;

        glm::vec4 m_genColor{1.0f, 1.0f, 1.0f, 1.0f};  ///< Edited in a tile's "solid color" generator.

        bool        m_renameOpen     = false;
        char        m_renameBuf[128] = {};
        std::string m_renameOldName;

        char m_chooserFilter[48] = {};  ///< Chooser combo search

        std::vector<EntityId>                  m_users;     ///< The entities drawing the material shown;
                                                            ///< walked again when m_usersKey moves.
        std::optional<std::array<uint64_t, 4>> m_usersKey;  ///< Empty until the first walk.
};

} // namespace Vkm::Engine
