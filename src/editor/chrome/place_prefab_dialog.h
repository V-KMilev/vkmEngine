#pragma once

#include "ui/asset_picker.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

/**
 * @brief The "Prefab" picker, and the placing of what is picked.
 *
 * Drawn at the root window's scope for the same reason as ModelImportDialog.
 */
class PlacePrefabDialog {
    public:
        PlacePrefabDialog() = default;
        ~PlacePrefabDialog() = default;

        PlacePrefabDialog(const PlacePrefabDialog& other) = delete;
        PlacePrefabDialog& operator=(const PlacePrefabDialog& other) = delete;

        PlacePrefabDialog(PlacePrefabDialog && other) = delete;
        PlacePrefabDialog& operator=(PlacePrefabDialog && other) = delete;

    public:
        /**
         * @brief Open the picker when EditorState::requestPlacePrefab is set,
         *        and instance the prefab through EditorActions::placePrefab.
         *
         * With no prefabs yet, toasts where they come from instead.
         *
         * @param scene Receives the instance.
         * @param resources Resolves the prefab's asset names to handles.
         * @param state Carries the request; receives the undo step and selection.
         */
        void draw(Scene& scene, ResourceManager& resources, EditorState& state);

    private:
        AssetPicker m_picker;
};

} // namespace Vkm::Engine
