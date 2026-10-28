#include "chrome/place_prefab_dialog.h"

#include <filesystem>
#include <string>
#include <system_error>

#include "editor_actions.h"
#include "editor_state.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

namespace {
bool hasAnyPrefab() {
    namespace fs = std::filesystem;
    std::error_code ec;
    // Recursive, like the picker. Stepped by hand: a range-for's error_code
    // covers the constructor alone.
    for (fs::recursive_directory_iterator it(ProjectPaths::prefabs(), ec), end;
         !ec && it != end; it.increment(ec)) {
        // Its own error_code, or one unreadable entry ends the walk and this
        // answers "no prefabs".
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && it->path().extension() == ".json") return true;
    }
    return false;
}
} // namespace

void PlacePrefabDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestPlacePrefab) {
        state.requestPlacePrefab = false;
        if (hasAnyPrefab()) {
            // Project-relative, the default: what the instance stores, resolvable on another machine.
            AssetPicker::Options options;
            options.title      = "Place Prefab";
            options.root       = ProjectPaths::prefabs();
            options.recursive  = true;
            options.extensions = {".json"};
            m_picker.open(options);
        } else {
            state.pushToast(ToastKind::Info, "No prefabs yet - right-click an entity and Save as Prefab");
        }
    }

    std::string picked;
    if (m_picker.draw(picked)) EditorActions::placePrefab(scene, resources, state, picked);
}

} // namespace Vkm::Engine
