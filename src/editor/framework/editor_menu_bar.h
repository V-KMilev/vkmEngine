#pragma once

namespace Vkm::Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief The editor's top menu bar (File / Edit / View / Window / Entity / Help).
 *
 * Holds no command state and owns nothing but a popup flag: it reads and writes
 * EditorState and forwards scene-file intents to the SceneIOController. Draws
 * inside the root window's menu-bar scope with strict ordering (like the
 * viewport overlays), called once per frame between Begin("##Editor") and the
 * panel layout.
 *
 * draw() also renders the scene Save-As / Load dialogs (via the controller)
 * so they stay in the same menu-bar scope they were before.
 *
 * Note: the Import Model dialog is owned by EditorSystem (not the menu bar)
 * because both the Inspector empty-state and the Hierarchy "+" menu also
 * raise `state.requestModelImport`. The menu bar just toggles the flag.
 */
class EditorMenuBar {
    public:
        void draw(EditorContext& ec, SceneIOController& sceneIO);

    private:
        bool m_openAbout = false;  ///< About requested this frame; popup opens at menu-bar scope.
};

} // namespace Vkm::Engine
