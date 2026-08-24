#pragma once

#include <string>

#include "framework/asset_picker.h"
#include "resource/resource_handle.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief Floating Asset Browser: a live thumbnail grid of materials & meshes, plus a list of sounds.
 *
 * Opened from Window > Asset Browser. Each cell is a material/mesh preview via
 * MaterialPreviewSession, budgeted so a big grid spreads its bakes over several
 * frames.
 *
 * Sounds get a list rather than a grid, because a clip has no picture. What it
 * has is a length, a layout and a sound, so the row shows the first two and a
 * transport gives the third - hearing a clip IS previewing it, the way a
 * thumbnail is for a material.
 *
 * Stateless w.r.t. assets - it reads ResourceManager every frame; only the
 * cell size and the two cached helper assets (a preview sphere for material
 * thumbnails, a neutral material for mesh thumbnails) live here.
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
        void draw(EditorContext& ec);

    private:
        // Which asset family the rename modal currently targets.
        enum class RenameKind { Material, Mesh };

        void ensureAssets(ResourceManager& resources);
        void drawMaterials(EditorContext& ec);
        void drawMeshes(EditorContext& ec);
        void drawSounds(EditorContext& ec);

        // The single grid body shared by materials and meshes. Asset is the
        // asset type (MaterialAsset / MeshAsset); the matching handle is
        // Handle<Asset>. The material-only extras (an "Open in Material Editor"
        // context item and left-click-to-edit) are guarded with if constexpr.
        template<typename Asset>
        void drawAssetGrid(EditorContext& ec);

        /**
         * @brief Arm the shared rename modal for one asset.
         *
         * @tparam Asset Asset family being renamed (MaterialAsset or MeshAsset),
         *         recorded as the modal's RenameKind tag.
         * @param h Handle of the asset to rename; its key seeds the modal target.
         * @param name Current name, copied into the edit buffer as the starting text.
         */
        template<typename Asset>
        void openRename(Handle<Asset> h, const std::string& name);

    private:
        float      m_cell = 104.0f;          ///< Thumbnail edge in px
        char       m_filter[64] = {};        ///< Grid search needle (every tab)

        // Sound import + audition. The picker is panel-owned so its popup id is
        // unique; the voice is remembered so a second play replaces the first
        // rather than layering a copy over it.
        AssetPicker m_soundPicker;
        bool        m_requestSoundImport = false;
        VoiceId     m_previewVoice = 0;

        // Which clip that voice came from, so the row playing it is the row
        // that can hold it, cut it short and scrub it, while the rest offer
        // only Play. The full handle rather than the id: a slot recycled by a
        // remove and an add would otherwise hand a different clip a transport
        // running against somebody else's sound. A graph swapped underneath it
        // cannot: AudioSystem stops every voice when the asset epoch moves, so
        // a handle from the manager that went away can never read as sounding.
        AudioClipHandle m_previewClip;

        // Re-acquired every draw via ensureAssets - no ready flag so a
        // ResourceManager swap (scene load) doesn't leave stale handles.
        MeshHandle     m_sphere;             ///< Shape for material thumbnails
        MaterialHandle m_neutral;            ///< Material for mesh thumbnails

        // Rename modal state - materials and meshes share one popup; the kind
        // tag plus the handle key identify the target while the modal is open.
        // The full StorageIndex (index + generation) is kept so the handle
        // reconstructs faithfully for rename/command.
        char           m_renameBuf[128] = {};
        std::string    m_renameOldName;     ///< name before the edit, for undo
        RenameKind     m_renameKind = RenameKind::Material;
        StorageIndex   m_renameKey  = {};   ///< handle key of the rename target
        bool           m_renameOpen = false;
};

} // namespace Vkm::Engine
