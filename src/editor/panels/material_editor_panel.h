#pragma once

#include <string>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"
#include "framework/asset_picker.h"

namespace Vkm::Engine {

class EditorRenderHooks;

struct EditorContext;
class ResourceManager;

/**
 * @brief The Material tab: one material's parameters under a live 3D preview.
 *
 * It has no window of its own. EditorSystem draws it as the right panel's
 * second tab, beside the Inspector, because the two answer the same question
 * about the same selection at two depths - and because a material wants the
 * height a panel next to the viewport has and the bottom strip does not. A
 * second way in would be a second place to fix everything found here.
 *
 * Which material it edits follows the selection, the way every other surface in
 * this panel does: picking an entity that carries one shows that one. A
 * material chosen by hand - from the chooser at the top, the Asset Browser, or
 * a New / Duplicate - outranks the selection until a different entity with a
 * material is picked, so a material nothing uses yet can still be worked on.
 *
 * The parameters are cut by what a material actually is rather than by the
 * shader's field list: a Base card and a grid of map tiles are always there,
 * and the five secondary lobes appear as cards only once the material uses
 * them. "Add Feature" turns one on, the card's own x turns it back off, and
 * nothing is offered twice.
 *
 * Edits are live: materials are shared by handle, so a change shows in the
 * preview and everywhere the material is drawn, and every one of them is an
 * undo step. The 3D preview goes through MaterialPreviewSession (which renders
 * via the backend's offscreen preview hooks) and is shown via ImGui::Image -
 * the editor never touches GL itself.
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
         * @param ec Per-frame editor context.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Settle which material this frame edits, and drop a dead pin.
         *
         * @param ec Per-frame editor context; its selection and pinned target
         *        are both read, and the pin is cleared when the selection moves
         *        to another entity carrying a material.
         * @return The material to edit, or a null handle when there is none.
         */
        MaterialHandle resolveTarget(EditorContext& ec);

        /**
         * @brief Draw the panel with no material to edit: the two ways to get one.
         *
         * @param ec Per-frame editor context.
         */
        void drawEmptyState(EditorContext& ec);

        /**
         * @brief Draw the chooser row: which material, who uses it, what can be
         * done to it.
         *
         * @param ec Per-frame editor context.
         * @param target The material being edited.
         */
        void drawIdentityRow(EditorContext& ec, MaterialHandle target);

        /**
         * @brief Draw the material chooser combo, filling the given width.
         *
         * @param ec Per-frame editor context; a pick writes its target.
         * @param target The material being edited, previewed in the combo.
         * @param width Width the combo fills.
         */
        void drawChooser(EditorContext& ec, MaterialHandle target, float width);

        /**
         * @brief Draw the live preview, its shape and background controls and the
         * light dial.
         *
         * @param ec Per-frame editor context.
         * @param target The material being previewed.
         * @param entityMesh Mesh of the selected entity, for the "as selected"
         *        shape; a null handle disables that shape.
         */
        void drawPreview(EditorContext& ec, MaterialHandle target, MeshHandle entityMesh);

        /**
         * @brief Resolve the preview shape to a real MeshAsset handle.
         *
         * Looks the shape up by name every call (O(1)) and lazily re-registers
         * it if absent, so it survives the ResourceManager swap a scene load
         * performs (which drops every hidden asset).
         *
         * @param resources Resource manager the shapes are registered in.
         * @param entityMesh Mesh to use for the "as selected" shape.
         * @return The shape to render the material on.
         */
        MeshHandle previewMesh(ResourceManager& resources, const MeshHandle& entityMesh);

        /**
         * @brief Draw every parameter card for one material.
         *
         * @param resources Resource manager used to resolve and edit texture slots.
         * @param backend Resolves a slot's GPU texture for its tile; may be null.
         * @param target Handle of the material being edited (stable across slot
         *        reallocations).
         * @param mat The material asset whose fields the controls write to.
         * @return Whether any field changed this frame.
         */
        bool drawParameters(ResourceManager& resources, EditorRenderHooks* backend,
                            MaterialHandle target, MaterialAsset& mat);

        /**
         * @brief Draw the map grid: the core slots, whatever else is bound, and
         * the tile that adds one.
         *
         * @param resources Resource manager used to resolve and edit texture slots.
         * @param backend Resolves a slot's GPU texture for its tile; may be null.
         * @param target Handle of the material being edited.
         * @param mat The material asset whose slots the tiles write to.
         * @return Whether any slot changed this frame.
         */
        bool drawMaps(ResourceManager& resources, EditorRenderHooks* backend,
                      MaterialHandle target, MaterialAsset& mat);

        /**
         * @brief One map tile: the bound texture's thumbnail, its name, its file.
         *
         * Takes the owning material's handle and a pointer-to-member rather than
         * a slot reference so a deferred picker can re-resolve the slot safely -
         * the sparse-set backing can reallocate while the picker is open.
         *
         * @param resources Resource manager used to resolve and edit the slot.
         * @param backend Resolves the slot's GPU texture; may be null (no thumbnail).
         * @param owner Handle of the material the slot belongs to.
         * @param mat The material asset being edited.
         * @param label What the slot is called.
         * @param member The slot itself.
         * @param srgb Whether a texture bound here is colour data.
         * @param hint What the slot does, shown on hover.
         * @param face Tile edge length in pixels.
         * @return Whether the slot changed this frame.
         */
        bool mapTile(ResourceManager& resources, EditorRenderHooks* backend,
                     MaterialHandle owner, MaterialAsset& mat, const char* label,
                     TextureHandle MaterialAsset::* member, bool srgb,
                     const char* hint, float face);

        /**
         * @brief Arm the texture picker for one slot.
         *
         * @param owner Handle of the material the slot belongs to.
         * @param member The slot to fill once a file is chosen.
         * @param srgb Whether the texture is colour data.
         */
        void openTexturePicker(MaterialHandle owner, TextureHandle MaterialAsset::* member,
                               bool srgb);

        /**
         * @brief Bind whatever the texture picker returned, as an undo step.
         *
         * @param ec Per-frame editor context.
         */
        void serviceTexturePicker(EditorContext& ec);

        /**
         * @brief Run the "Load PBR Folder" picker and adopt the material it builds.
         *
         * @param ec Per-frame editor context; its target follows the new material.
         */
        void servicePbrFolder(EditorContext& ec);

    private:
        // Orbit / zoom state for the preview camera.
        float m_yaw      = 35.0f;
        float m_pitch    = 20.0f;
        float m_distance = 3.0f;
        int   m_shape      = 0;    ///< Index into the preview shape table
        int   m_background = 0;    ///< PreviewBackground: 0 dark, 1 grey, 2 sky
        float m_lightYaw   = 0.0f; ///< Studio rig rotation around Y (degrees)

        // The selection the pinned target was last reconciled against. A pin
        // outranks the selection, but only until the author picks a different
        // entity that carries a material of its own.
        EntityId m_lastSelection{};

        // One picker per modal so each cache survives independent open/close.
        AssetPicker m_pbrFolderPicker;
        AssetPicker m_texturePicker;
        bool        m_requestPbrFolder = false;

        /**
         * @brief The material + slot the active texture picker is editing,
         * identified by handle + pointer-to-member (not a raw pointer) so it
         * survives a sparse-set reallocation. m_pendingSlot null == no picker.
         */
        MaterialHandle                  m_pendingMaterial{};
        TextureHandle MaterialAsset::*  m_pendingSlot = nullptr;
        bool                            m_pendingTextureSrgb = false;

        /// Colour edited in a tile's "solid colour" generator.
        glm::vec4 m_genColor{1.0f, 1.0f, 1.0f, 1.0f};

        // Rename modal state, armed from the actions menu.
        bool        m_renameOpen = false;
        char        m_renameBuf[128] = {};
        std::string m_renameOldName;

        char m_chooserFilter[48] = {};  ///< Search needle inside the chooser combo
};

} // namespace Vkm::Engine
