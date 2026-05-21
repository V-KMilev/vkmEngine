#pragma once

#include "input/editor_actions.h"  // ModelImportDialog

namespace Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief The editor's top menu bar (File / Edit / View / Scene / Help).
 *
 * Extracted from EditorSystem (god-file decomposition). Stateless command
 * surface: it reads/writes EditorState and forwards scene-file intents to
 * the SceneIOController. Draws inside the root window's menu-bar scope
 * with strict ordering (like the viewport overlays), called once per frame
 * between Begin("##Editor") and the panel layout.
 *
 * draw() also renders the scene Save-As / Load dialogs (via the controller)
 * so they stay in the same menu-bar scope they were before.
 */
class EditorMenuBar {
    public:
        void draw(EditorContext& ec, SceneIOController& sceneIO);
    private:
        EditorActions::ModelImportDialog m_modelImport;
};

} // namespace Engine
