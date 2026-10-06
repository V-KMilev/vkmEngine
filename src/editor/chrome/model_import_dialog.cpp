#include "chrome/model_import_dialog.h"

#include <filesystem>
#include <memory>
#include <string>

#include "command/editor_commands.h"
#include "editor_state.h"
#include "import/model_loaders.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        AssetPicker::Options options;
        options.title      = "Import Model";
        options.root       = ProjectPaths::assets();
        options.recursive  = true;
        options.extensions = {".gltf", ".glb", ".obj", ".fbx"};
        options.maxResults = 2000;
        options.hint       = "glTF / GLB / OBJ / FBX";
        m_picker.open(options);
        state.requestModelImport = false;
    }
    std::string picked;
    if (m_picker.draw(picked)) {
        const ModelImport imported = importModelIntoScene(picked, resources, scene);
        if (imported.root) {
            // An undo step: the root may have taken the slot a deleted entity's
            // undo is waiting to restore into.
            state.pushStep(
                std::make_unique<CreateSubtreeCommand>(
                    SubtreeSnapshot::capture(scene, imported.root),
                    "Import Model"
                )
            );
            state.selectEntity(imported.root);
        } else if (imported.ok) {
            // Clips and no mesh: nothing to select.
            const std::string message = "Imported " + std::to_string(imported.clips)
                + " clip(s). The file has no mesh, so nothing was added to the scene.";
            state.pushToast(ToastKind::Info, message);
        } else {
            state.pushToast(ToastKind::Error, "Could not import '" + picked + "'");
        }
    }
}

} // namespace Vkm::Engine
