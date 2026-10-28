#include "asset_editors/material_editor.h"

#include "asset_editors/asset_editors.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

MaterialEditor::MaterialEditor(StorageIndex key)
    : AssetEditor(AssetType::Material, key)
    , m_panel(MaterialHandle{key}, AssetEditors::viewportKey(AssetType::Material, key))
{}

const Resource* MaterialEditor::asset(const ResourceManager& resources) const {
    return resources.tryGet(MaterialHandle{key()});
}

void MaterialEditor::draw(EditorContext& ec) {
    m_panel.draw(ec);
}

} // namespace Vkm::Engine
