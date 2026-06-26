#pragma once

#include <string>

#include "resource/resource_handle.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"

namespace Engine {

struct EditorContext;
class ResourceManager;

/**
 * @brief Floating Asset Browser: a live thumbnail grid of materials & meshes.
 *
 * Opened from Window > Asset Browser. Each cell is a material/mesh preview via
 * MaterialPreviewSession, budgeted so a big grid spreads its bakes over several
 * frames. Click a material to edit it (opens the Material Editor); "Assign"
 * applies a material/mesh to the selected entity's Mesh. "Import Model..."
 * reuses the existing model-import dialog.
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

        // The single grid body shared by materials and meshes. Asset is the
        // asset type (MaterialAsset / MeshAsset); the matching handle is
        // Handle<Asset>. The material-only extras (an "Open in Material Editor"
        // context item and left-click-to-edit) are guarded with if constexpr.
        template<typename Asset>
        void drawAssetGrid(EditorContext& ec);

        /** @brief Arm the shared rename modal for one asset. */
        template<typename Asset>
        void openRename(Handle<Asset> h, const std::string& name);

        float      m_cell = 104.0f;          ///< Thumbnail edge in px

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

} // namespace Engine
