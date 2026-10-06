#pragma once

#include "ui/asset_picker.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

/**
 * @brief The "Import Model" picker, and the import of what is picked.
 *
 * Drawn at the root window's scope, not inside the Create menu, so the modal
 * survives that menu closing.
 */
class ModelImportDialog {
    public:
        ModelImportDialog() = default;
        ~ModelImportDialog() = default;

        ModelImportDialog(const ModelImportDialog& other) = delete;
        ModelImportDialog& operator=(const ModelImportDialog& other) = delete;

        ModelImportDialog(ModelImportDialog && other) = delete;
        ModelImportDialog& operator=(ModelImportDialog && other) = delete;

    public:
        /**
         * @brief Open the picker when EditorState::requestModelImport is set, and import the pick.
         *
         * @param scene Receives the model.
         * @param resources Receives its meshes and materials.
         * @param state Carries the request; receives the undo step and selection.
         */
        void draw(Scene& scene, ResourceManager& resources, EditorState& state);

    private:
        AssetPicker m_picker;
};

} // namespace Vkm::Engine
