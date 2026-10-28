#pragma once

#include "asset_editors/asset_editor.h"
#include "panels/material_editor_panel.h"

namespace Vkm::Engine {

/**
 * @brief A material in a window of its own: its preview to orbit, its maps and parameters,
 *        whatever is selected.
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
        MaterialEditorPanel m_panel;
};

} // namespace Vkm::Engine
