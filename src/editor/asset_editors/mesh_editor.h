#pragma once

#include "asset_editors/asset_editor.h"
#include "resource/asset/material_asset.h"
#include "ui/preview_viewport.h"

namespace Vkm::Engine {

/**
 * @brief A mesh alone: turned, framed and lit in a viewport of its own, beside what it is made of.
 *
 * It previews under a material picked here - a plain grey until one is - and can be placed in
 * the scene where the editor's view looks.
 */
class MeshEditor : public AssetEditor {
    public:
        /**
         * @brief An editor of the mesh at @p key.
         *
         * @param key Its handle's slot and generation.
         */
        explicit MeshEditor(StorageIndex key);
        ~MeshEditor() override = default;

        MeshEditor(const MeshEditor& other) = delete;
        MeshEditor& operator=(const MeshEditor& other) = delete;

        MeshEditor(MeshEditor && other) = delete;
        MeshEditor& operator=(MeshEditor && other) = delete;

    public:
        const Resource* asset(const ResourceManager& resources) const override;
        void            draw(EditorContext& ec) override;

    private:
        /// The facts down the side: its geometry, its rig, and what it previews in.
        void drawDetails(EditorContext& ec);

    private:
        PreviewViewport m_viewport;
        MaterialHandle  m_look;  ///< What it previews in; none for the neutral grey.
};

} // namespace Vkm::Engine
