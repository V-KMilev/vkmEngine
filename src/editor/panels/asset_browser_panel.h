#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ui/asset_picker.h"
#include "io/asset/asset_serializer.h"
#include "resource/asset_type.h"
#include "resource/resource_handle.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct EditorContext;
struct EditorState;
class ResourceManager;
class Scene;
struct EntityId;

/**
 * @brief The Asset Browser: one library of every asset kind the project holds.
 *
 * A kind rail, then the chosen kind as a grid of tiles: a thumbnail, or the kind's glyph.
 * The grid is the project's library, by name: what this scene has not loaded shows dimmed, and is
 * loaded as it is used - opened, dragged out or assigned. Kinds are a table of AssetKind
 * descriptors (function pointers), not templates, so a kind lacking a thumbnail or an assign
 * target joins the same grid. Shows only the kinds AssetType names; why FontAsset is not here is
 * in docs/reference/editor.md.
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

        /**
         * @brief Make the viewport's rect a drop target for an asset dragged from a tile.
         *
         * Dropped on an entity its kind assigns to (a material on a mesh, a sound on an
         * Audio Source), it is assigned there; a mesh always, and a sound elsewhere, becomes
         * a new entity where the cursor's ray meets the scene (EditorActions::createFromAsset).
         * Call inside the viewport's window, after ec's viewport rect is set.
         *
         * @param ec The frame's editor context.
         */
        static void acceptViewportDrop(EditorContext& ec);

        /**
         * @brief Inside another panel's drop target on @p entity's row: assign an asset dragged
         *        from a tile to it, as its kind assigns (a material or mesh on a Mesh, a sound on an
         *        Audio Source, a rig or clip on an Animator). A kind that does not assign to it is
         *        not accepted.
         *
         * @param scene     Holds the entity.
         * @param resources Holds the asset.
         * @param state     Takes the step and the selection.
         * @param entity    The row's entity.
         */
        static void acceptAssetOn(
            Scene& scene,
            ResourceManager& resources,
            EditorState& state,
            EntityId entity
        );

        /**
         * @brief Inside another panel's drop target with no entity of its own: an asset dragged
         *        from a tile becomes a new entity where the view looks (EditorActions::createFromAsset).
         *
         * @param scene     Receives the entity.
         * @param resources Holds the asset.
         * @param state     Takes the step and the selection, and says where the view looks.
         */
        static void acceptAssetCreate(Scene& scene, ResourceManager& resources, EditorState& state);

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

        /// What the rail's counts were taken at: the assets' and the library's revisions, and the
        /// search.
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
         * @param type The kind.
         * @return Sorted names.
         */
        const std::vector<std::string>& libraryNames(AssetType type);

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

        /// The library's names per kind, and the AssetLibrary::revision each was read at, plus one
        /// (zero: never read).
        std::array<std::vector<std::string>, static_cast<size_t>(AssetType::Count)> m_library;
        std::array<uint64_t, static_cast<size_t>(AssetType::Count)>                 m_libraryAt{};

        /// The library row whose load failed, not tried again until another fails.
        std::string m_unloadable;

        RailKey                                                    m_railKey;     ///< See RailKey.
        std::array<size_t, static_cast<size_t>(AssetType::Count)> m_railCounts{};  ///< Rows per kind.

        /// What the scene's entities and the loading screen reference, walked again only once the
        /// world, its assets, the history or the screen built moved since (m_refsKey), or every
        /// frame of a session, which edits outside it.
        AssetSerializer::EntityAssetRefs       m_refs;
        std::optional<std::array<uint64_t, 4>> m_refsKey;  ///< Empty until the first walk.
};

} // namespace Vkm::Engine
