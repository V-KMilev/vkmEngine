#pragma once

#include <string>

#include "framework/asset_picker.h"
#include "resource/asset_type.h"
#include "resource/resource_handle.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief The Asset Browser: one library of every asset kind the project holds.
 *
 * A kind rail down the left, and beside it the chosen kind as a grid of tiles.
 * Every kind gets the same tile - one square face, a name, a one-line detail -
 * filled by a rendered thumbnail where a kind has one and by the kind's glyph
 * where it does not, so a sound needs no picture invented for it to sit beside
 * a mesh.
 *
 * Deliberately not templated on an asset type: it walks a table of AssetKind
 * descriptors, each carrying that kind's enumerate, describe, preview, assign,
 * rename and delete as function pointers, so a kind with no thumbnail or nothing
 * on an entity to assign to joins the same grid instead of being given a tab of
 * its own. It shows the six kinds AssetType names and only those; why FontAsset
 * is a Resource that does not belong here is in docs/reference/editor.md.
 *
 * It has no window of its own - BottomPanel draws it as a tab - and keeps no
 * asset state: only the chosen kind, the tile size, the audition voice and the
 * two cached helper assets the thumbnails are rendered with.
 */
class AssetBrowserPanel {
    public:
        AssetBrowserPanel() = default;
        ~AssetBrowserPanel() = default;

        AssetBrowserPanel(const AssetBrowserPanel& other) = delete;
        AssetBrowserPanel& operator=(const AssetBrowserPanel& other) = delete;

        AssetBrowserPanel(AssetBrowserPanel && other) = delete;
        AssetBrowserPanel& operator=(AssetBrowserPanel && other) = delete;

    public:
        /**
         * @brief Draw the whole panel into the caller's current region.
         *
         * @param ec Per-frame editor context.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief The asset a modal is armed on: its kind, its slot and its name.
         *
         * The kind tag is what lets a modal reconstruct the typed handle from
         * the slot; the slot is kept whole (index and generation) so a recycled
         * index cannot be mistaken for the asset that used to live in it. Both
         * modals target one asset and neither is reachable while the other is
         * open, so they carry that target one way rather than each growing a
         * kind, a key, a name and a flag of its own.
         */
        struct AssetTarget {
            AssetType    kind = AssetType::Material;
            StorageIndex key  = {};
            std::string  name;
            bool         open = false;
        };

        void ensureAssets(ResourceManager& resources);

        void drawRail(EditorContext& ec);
        void drawToolbar(EditorContext& ec);
        void drawGrid(EditorContext& ec);
        void drawRenameModal(EditorContext& ec);
        void drawDeleteModal(EditorContext& ec);
        /// Configure the shared picker for images and raise it.
        void openTextureImport();
        /// Configure the shared picker for audio files and raise it.
        void openSoundImport();
        void serviceTextureImport(EditorContext& ec);
        void serviceSoundImport(EditorContext& ec);

        /**
         * @brief Arm the shared rename modal for one asset.
         *
         * @param kind Asset kind being renamed; decides which handle the modal
         *        reconstructs on confirm.
         * @param key Storage index of the asset.
         * @param name Current name, copied into the edit buffer as the start text.
         */
        void openRename(AssetType kind, StorageIndex key, const std::string& name);

    private:
        AssetType m_kind = AssetType::Material;  ///< Rail row currently shown
        float     m_cell = 104.0f;               ///< Tile face edge, in EditorStyle::px units
        char      m_filter[64] = {};             ///< Search needle (narrows rail and grid alike)

        // Imports this panel runs itself rather than through an EditorState
        // flag. One picker each: panel-owned so their popup ids are unique, and
        // separate so one in flight cannot be handed the other's file.
        AssetPicker m_texturePicker;
        AssetPicker m_soundPicker;

        // The audition voice is remembered so a second play replaces the first
        // rather than layering a copy over it.
        VoiceId     m_previewVoice = 0;

        // Which clip that voice came from, so only the tile playing it can hold,
        // cut or scrub it. The full handle rather than the id, because a recycled
        // slot would hand a different clip somebody else's transport.
        AudioClipHandle m_previewClip;

        // Re-acquired every draw via ensureAssets - no ready flag so a
        // ResourceManager swap (scene load) doesn't leave stale handles.
        MeshHandle     m_sphere;             ///< Shape for material thumbnails
        MaterialHandle m_neutral;            ///< Material for mesh thumbnails

        // Every kind shares these two popups. The rename buffer is the only
        // thing either modal needs beyond its target, so it sits beside the
        // target it edits rather than inside AssetTarget, which the delete
        // modal would then carry unused.
        AssetTarget m_rename;
        AssetTarget m_delete;
        char        m_renameBuf[128] = {};
};

} // namespace Vkm::Engine
