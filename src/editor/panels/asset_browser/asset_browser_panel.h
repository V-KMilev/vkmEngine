#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vkm_audio/device.h"

#include "ui/file_picker.h"
#include "io/scene/asset_refs.h"
#include "resource/asset_type.h"
#include "resource/handle.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

struct AssetKind;
struct AssetRow;
struct EditorContext;
struct EditorState;
class ResourceManager;
class Scene;
struct EntityId;
struct TileContext;

/**
 * @brief The Asset Browser: a rail of the asset kinds AssetType names, and the project's library of one.
 *
 * What this scene has not loaded shows dimmed and loads as it is used. Why FontAsset is not a kind is
 * in docs/reference/editor.md. Draws into the Assets window EditorSystem begins.
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
        // The tile sizes Ctrl+wheel steps through, in EditorStyle::px units.
        static constexpr float MIN_CELL  = 64.0f;
        static constexpr float MAX_CELL  = 200.0f;
        static constexpr float CELL_STEP = 8.0f;

    private:
        /**
         * @brief The asset a modal is armed on; its kind rebuilds the typed handle.
         */
        struct AssetTarget {
            AssetType    kind = AssetType::Material;
            StorageIndex key  = {};  ///< Generation included: a recycled slot is not the old asset.
            std::string  name;
            bool         open = false;
        };

        /**
         * @brief What the rail's counts were taken at: the assets' and library's revisions, and the search.
         */
        struct RailKey {
            uint64_t    resources = ~0ull;
            uint64_t    library   = ~0ull;
            std::string filter;

            bool operator!=(const RailKey& other) const {
                return resources != other.resources || library != other.library || filter != other.filter;
            }
        };

        void ensureAssets(ResourceManager& resources);

        /**
         * @brief The library's names of @p type, read again only once the library changed.
         *
         * @param type The kind, each cached apart.
         * @return Sorted names.
         */
        const std::vector<std::string>& libraryNames(AssetType type);

        void drawRail(EditorContext& ec);
        void drawToolbar(EditorContext& ec);
        void drawGrid(EditorContext& ec);

        /**
         * @brief Walk what the scene and the loading screen reference into m_refs, once either changed.
         *
         * @param ec The frame's editor context; every frame in play, which edits outside the history.
         */
        void collectRefs(EditorContext& ec);

        /**
         * @brief A row's asset, loading a library row's as it is first used.
         *
         * A load that fails is not tried again until the left button is let go: once a drag.
         *
         * @param ec   Its resources take the asset; its toasts take a failure.
         * @param kind The row's kind.
         * @param row  The row.
         * @return Its key, or a null one.
         */
        StorageIndex takeRow(EditorContext& ec, const AssetKind& kind, const AssetRow& row);

        /**
         * @brief Draw one tile: its face, the lines under it, its tooltip, menu and drag.
         *
         * @param tc   The frame's preview context; a texture's upload is taken from its budget.
         * @param kind The grid's kind.
         * @param row  The tile's asset.
         */
        void drawTile(TileContext& tc, const AssetKind& kind, const AssetRow& row);

        /// A tile's context menu: load, open and assign its asset, and drawHeldItems for a loaded one.
        void drawTileMenu(EditorContext& ec, const AssetKind& kind, const AssetRow& row, bool orphan);

        /// A loaded asset's own menu items: renaming and deleting it, and selecting what uses it.
        void drawHeldItems(EditorContext& ec, const AssetKind& kind, const AssetRow& row, bool orphan);

        /// The two lines under a tile's face; @p mine: the audition is this tile's clip.
        void drawTileCaption(EditorContext& ec, const AssetKind& kind, const AssetRow& row, bool mine);

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
         * @param key  The asset's slot, generation included.
         * @param name Current name, the edit buffer's start text.
         */
        void openRename(AssetType kind, StorageIndex key, const std::string& name);

    private:
        AssetType m_kind       = AssetType::Material;  ///< Rail row shown
        float     m_cell       = 104.0f;               ///< Tile face edge, in EditorStyle::px units
        bool      m_unusedOnly = false;                ///< The grid shows only what nothing uses
        char      m_filter[64] = {};                   ///< Search; narrows rail and grid

        // Panel-owned for unique popup ids; separate so one cannot be handed the other's file.
        FilePicker m_texturePicker;
        FilePicker m_soundPicker;

        Audio::VoiceId  m_previewVoice = 0;  ///< So a second play replaces the first rather than layering.
        AudioClipHandle m_previewClip;       ///< The voice's clip, so only its tile drives it; a full
                                             ///< handle, as a recycled slot would hand another clip the
                                             ///< transport.

        // Re-acquired every draw by ensureAssets, so a ResourceManager::swap leaves none stale.
        MeshHandle     m_sphere;             ///< Shape for material thumbnails
        MaterialHandle m_neutral;            ///< Material for mesh thumbnails

        AssetTarget m_rename;
        AssetTarget m_delete;
        char        m_renameBuf[128] = {};

        std::vector<uint8_t> m_used;  ///< Scratch for the grid's "in use" marks, kept for its capacity.

        // Per kind: the library's names, and the AssetLibrary::revision each was read at plus one (0: never).
        std::array<std::vector<std::string>, static_cast<size_t>(AssetType::Count)> m_library;  ///< Sorted.
        std::array<uint64_t, static_cast<size_t>(AssetType::Count)>                 m_libraryAt{};

        std::string m_unloadable;  ///< The library row whose load failed; not tried again while the button
                                   ///< stays held.

        RailKey                                                   m_railKey;       ///< Keys m_railCounts.
        std::array<size_t, static_cast<size_t>(AssetType::Count)> m_railCounts{};  ///< Rows per kind.

        SceneAssets::EntityAssetRefs           m_refs;     ///< What the scene and the loading screen
                                                           ///< reference; walked again when m_refsKey moves.
        std::optional<std::array<uint64_t, 4>> m_refsKey;  ///< Empty until the first walk.
};

} // namespace Vkm::Engine
