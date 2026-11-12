#include "chrome/dialog/place_prefab_dialog.h"

#include <string>

#include "actions/prefab_actions.h"
#include "editor_state.h"
#include "io/project/roots.h"

namespace Vkm::Engine {

void PlacePrefabDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requests.take(Ask::PlacePrefab)) {
        if (!EditorActions::listPrefabs().empty()) {
            // Project-relative, the default: what the instance stores, resolvable on another machine.
            FilePicker::Options options;
            options.title      = "Place Prefab";
            options.root       = Roots::prefabs();
            options.recursive  = true;
            options.extensions = {EditorActions::PREFAB_EXTENSION};
            m_picker.open(options);
        } else {
            state.toasts.push(ToastKind::Info, "No prefabs yet - right-click an entity and Save as Prefab");
        }
    }

    std::string picked;
    if (m_picker.draw(picked)) EditorActions::placePrefab(scene, resources, state, picked);
}

} // namespace Vkm::Engine
