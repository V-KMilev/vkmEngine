#include "chrome/dialog/model_import_dialog.h"

#include <string>
#include <vector>

#include "session/asset/file_import.h"
#include "editor_state.h"
#include "import/import_formats.h"
#include "io/project/roots.h"

namespace Vkm::Engine {

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requests.take(Ask::ImportModel)) {
        FilePicker::Options options;
        options.title      = "Import Model";
        options.root       = Roots::assets();
        options.recursive  = true;
        options.extensions.assign(MODEL_EXTENSIONS.begin(), MODEL_EXTENSIONS.end());
        options.maxResults = 2000;
        options.hint       = "glTF / GLB / OBJ / FBX; Ctrl or Shift picks several";
        options.many       = true;
        m_picker.open(options);
    }
    std::vector<std::string> picked;
    if (!m_picker.draw(picked)) return;
    for (const std::string& path : picked) FileImport::importModel(path, scene, resources, state);
}

} // namespace Vkm::Engine
