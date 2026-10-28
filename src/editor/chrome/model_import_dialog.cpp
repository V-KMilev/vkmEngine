#include "chrome/model_import_dialog.h"

#include <string>

#include "session/file_import.h"
#include "editor_state.h"
#include "import/import_formats.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        AssetPicker::Options options;
        options.title      = "Import Model";
        options.root       = ProjectPaths::assets();
        options.recursive  = true;
        options.extensions.assign(MODEL_EXTENSIONS.begin(), MODEL_EXTENSIONS.end());
        options.maxResults = 2000;
        options.hint       = "glTF / GLB / OBJ / FBX";
        m_picker.open(options);
        state.requestModelImport = false;
    }
    std::string picked;
    if (m_picker.draw(picked)) FileImport::importModel(picked, scene, resources, state);
}

} // namespace Vkm::Engine
