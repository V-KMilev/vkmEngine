#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/asset_picker.h"
#include "resource/asset_type.h"
#include "resource/resource_handle.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief The Asset Browser: one library of every asset kind the project holds.
 *
 * A kind rail, then the chosen kind as a grid of tiles: a thumbnail, or the kind's glyph.
 * Kinds are a table of AssetKind descriptors (function pointers), not templates, so a
 * kind lacking a thumbnail or an assign target joins the same grid. Shows only the
 * kinds AssetType names; why FontAsset is not here is in docs/reference/editor.md.
 *
 * Draws into the Assets window EditorSystem begins.
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
         * @param ec The frame's editor context.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief The asset a modal is armed on: its kind, its slot and its name.
         *
         * The kind rebuilds the typed handle; the slot keeps its generation so a recycled
         * index is not mistaken for the old asset.
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
        /// Configure the picker for images and raise it.
        void openTextureImport();
        /// Configure the picker for audio files and raise it.
        void openSoundImport();
        void serviceTextureImport(EditorContext& ec);
        void serviceSoundImport(EditorContext& ec);

        /**
         * @brief Arm the shared rename modal for one asset.
         *
         * @param kind Decides which handle the modal rebuilds on confirm.
         * @param key The asset's storage index.
         * @param name Current name, the edit buffer's start text.
         */
        void openRename(AssetType kind, StorageIndex key, const std::string& name);

    private:
        AssetType m_kind = AssetType::Material;  ///< Rail row shown
        float     m_cell = 104.0f;               ///< Tile face edge, in EditorStyle::px units
        char      m_filter[64] = {};             ///< Search; narrows rail and grid

        // Panel-owned for unique popup ids; separate so one cannot be handed the other's file.
        AssetPicker m_texturePicker;
        AssetPicker m_soundPicker;

        /// Remembered so a second play replaces the first rather than layering.
        VoiceId m_previewVoice = 0;

        /// The voice's clip, so only its tile drives it; a full handle, as a recycled slot
        /// would hand another clip the transport.
        AudioClipHandle m_previewClip;

        // Re-acquired every draw by ensureAssets, so a ResourceManager::swap leaves none stale.
        MeshHandle     m_sphere;             ///< Shape for material thumbnails
        MaterialHandle m_neutral;            ///< Material for mesh thumbnails

        AssetTarget m_rename;
        AssetTarget m_delete;
        char        m_renameBuf[128] = {};

        /// Per-frame scratch for the grid's "in use" marks, kept for its capacity.
        std::vector<uint8_t> m_used;
};

} // namespace Vkm::Engine
