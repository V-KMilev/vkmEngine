#include "chrome/place_prefab_dialog.h"

#include <string>

#include "editor_actions.h"
#include "editor_state.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

void PlacePrefabDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestPlacePrefab) {
        state.requestPlacePrefab = false;
        if (!EditorActions::listPrefabs().empty()) {
            // Project-relative, the default: what the instance stores, resolvable on another machine.
            AssetPicker::Options options;
            options.title      = "Place Prefab";
            options.root       = ProjectPaths::prefabs();
            options.recursive  = true;
            options.extensions = {EditorActions::PREFAB_EXTENSION};
            m_picker.open(options);
        } else {
            state.pushToast(ToastKind::Info, "No prefabs yet - right-click an entity and Save as Prefab");
        }
    }

    std::string picked;
    if (m_picker.draw(picked)) EditorActions::placePrefab(scene, resources, state, picked);
}

} // namespace Vkm::Engine
